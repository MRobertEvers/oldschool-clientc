import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Pattern;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.tree.ClassNode;
import org.objectweb.asm.tree.MethodNode;

/**
 * Diagnostic helper for bisecting a decompiler regression against golden
 * revision-239 bytecode. The output keeps the base class's fields/API and
 * replaces matching method bodies with bodies from the donor class.
 */
public final class Rl239TransplantMethods {
    private Rl239TransplantMethods() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            throw new IllegalArgumentException(
                "usage: Rl239TransplantMethods BASE.class DONOR.class OUT.class REGEX");
        }

        ClassNode base = read(Path.of(args[0]));
        ClassNode donor = read(Path.of(args[1]));
        if (!base.name.equals(donor.name)) {
            throw new IllegalArgumentException(base.name + " != " + donor.name);
        }

        Pattern wanted = Pattern.compile(args[3]);
        Map<String, MethodNode> replacements = new HashMap<>();
        for (MethodNode method : donor.methods) {
            String key = method.name + method.desc;
            if (wanted.matcher(key).matches()) {
                replacements.put(key, method);
            }
        }

        int changed = 0;
        for (int i = 0; i < base.methods.size(); i++) {
            MethodNode method = base.methods.get(i);
            MethodNode replacement = replacements.remove(method.name + method.desc);
            if (replacement != null) {
                base.methods.set(i, replacement);
                changed++;
            }
        }
        if (!replacements.isEmpty()) {
            throw new IllegalArgumentException("donor-only methods: " + replacements.keySet());
        }
        if (changed == 0) {
            throw new IllegalArgumentException("regex selected no methods: " + args[3]);
        }

        // COMPUTE_MAXS is enough for the diagnostic JVM's -Xverify:none run.
        // Keeping this deliberately separate from a production build prevents
        // a bytecode overlay from being mistaken for the eventual source fix.
        ClassWriter writer = new ClassWriter(ClassWriter.COMPUTE_MAXS);
        base.accept(writer);
        Files.write(Path.of(args[2]), writer.toByteArray());
        System.out.println("transplanted " + changed + " method(s)");
    }

    private static ClassNode read(Path path) throws Exception {
        ClassNode node = new ClassNode();
        new ClassReader(Files.readAllBytes(path)).accept(node, ClassReader.EXPAND_FRAMES);
        return node;
    }
}
