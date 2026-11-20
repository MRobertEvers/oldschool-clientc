/*
 * Quest Helper -> Lua. See README.md beside this file.
 *
 * javac does the hard part: parse, run Lombok, attribute. Everything below
 * reads the typed JCTree and writes Lua that the runtime in
 * script/plugins/quest_helper/rt/ executes.
 */

import com.sun.source.tree.CompilationUnitTree;
import com.sun.source.tree.MemberReferenceTree;
import com.sun.tools.javac.api.JavacTaskImpl;
import com.sun.tools.javac.api.JavacTool;
import com.sun.tools.javac.code.Flags;
import com.sun.tools.javac.code.Kinds;
import com.sun.tools.javac.code.Scope;
import com.sun.tools.javac.code.Symbol;
import com.sun.tools.javac.code.Symbol.ClassSymbol;
import com.sun.tools.javac.code.Symbol.MethodSymbol;
import com.sun.tools.javac.code.Symbol.VarSymbol;
import com.sun.tools.javac.code.Symtab;
import com.sun.tools.javac.code.Type;
import com.sun.tools.javac.code.TypeTag;
import com.sun.tools.javac.code.Types;
import com.sun.tools.javac.tree.JCTree;
import com.sun.tools.javac.tree.JCTree.*;
import com.sun.tools.javac.tree.TreeInfo;
import com.sun.tools.javac.util.Context;

import javax.tools.Diagnostic;
import javax.tools.DiagnosticListener;
import javax.tools.JavaFileObject;
import javax.tools.StandardJavaFileManager;
import java.io.File;
import java.io.IOException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.*;
import java.util.stream.Collectors;

public class QhToLua
{
    static Types types;
    static Symtab syms;
    static Path srcRoot;          // .../src/main/java
    static Path outRoot;          // script/plugins/quest_helper/gen
    static String classpath;

    /* Every class declared in the sources, by symbol, with its tree and file. */
    static final Map<ClassSymbol, JCClassDecl> declared = new LinkedHashMap<>();
    static final Map<ClassSymbol, String> classModule = new HashMap<>();
    static final Map<ClassSymbol, String> classFile = new HashMap<>();
    static final Map<ClassSymbol, JCCompilationUnit> classUnit = new HashMap<>();
    /* Module -> the classes it depends on outside itself (for load order). */
    static final Map<String, Set<String>> moduleDeps = new TreeMap<>();
    static final Map<String, StringBuilder> moduleText = new TreeMap<>();

    /* external member inventory: "kind\towner\tluaName\tjava" -> count */
    static final Map<String, Integer> inventory = new TreeMap<>();
    static final Set<ClassSymbol> externalEnums = new TreeSet<>(Comparator.comparing(c -> c.flatname.toString()));
    static final Map<String, Integer> unresolved = new TreeMap<>();
    static final Map<String, String> aliases = new HashMap<>();

    static final String MANUAL_PREFIX = "com.questhelper.panel";

    public static void main(String[] args) throws Exception
    {
        String src = null, cp = null, ap = null, out = null, alias = null;
        for( int i = 0; i < args.length; i++ )
        {
            switch( args[i] )
            {
                case "--src": src = args[++i]; break;
                case "--cp": cp = args[++i]; break;
                case "--ap": ap = args[++i]; break;
                case "--out": out = args[++i]; break;
                case "--aliases": alias = args[++i]; break;
                default: throw new IllegalArgumentException(args[i]);
            }
        }
        srcRoot = Paths.get(src).toAbsolutePath();
        outRoot = Paths.get(out).toAbsolutePath();
        classpath = cp;
        if( alias != null )
            for( String line : Files.readAllLines(Paths.get(alias)) )
            {
                line = line.trim();
                if( line.isEmpty() || line.startsWith("#") ) continue;
                String[] kv = line.split("\\s+");
                aliases.put(kv[0], kv[1]);
            }

        List<File> files = new ArrayList<>();
        Files.walk(srcRoot).filter(p -> p.toString().endsWith(".java")).sorted().forEach(p -> files.add(p.toFile()));

        JavacTool tool = JavacTool.create();
        List<String> errors = new ArrayList<>();
        DiagnosticListener<JavaFileObject> diag = d -> {
            if( d.getKind() == Diagnostic.Kind.ERROR )
                errors.add(d.toString());
        };
        StandardJavaFileManager fm = tool.getStandardFileManager(diag, null, StandardCharsets.UTF_8);
        List<String> opts = new ArrayList<>(List.of(
            "-cp", cp, "-proc:full", "-processorpath", ap, "-nowarn", "-encoding", "UTF-8",
            "-XDshould-stop.ifError=FLOW", "-XDshould-stop.ifNoError=FLOW"));
        JavacTaskImpl task = (JavacTaskImpl) tool.getTask(null, fm, diag, opts, null, fm.getJavaFileObjectsFromFiles(files));
        Iterable<? extends CompilationUnitTree> cus = task.parse();
        task.analyze();
        for( String e : errors )
            System.err.println("javac: " + e.split("\n")[0]);
        Context ctx = task.getContext();
        types = Types.instance(ctx);
        syms = Symtab.instance(ctx);

        List<JCCompilationUnit> units = new ArrayList<>();
        for( CompilationUnitTree cu : cus ) units.add((JCCompilationUnit) cu);

        for( JCCompilationUnit cu : units )
        {
            Path file = Paths.get(cu.sourcefile.toUri()).toAbsolutePath();
            String module = moduleFor(file);
            for( JCTree def : cu.defs )
                if( def instanceof JCClassDecl )
                {
                    collect((JCClassDecl) def, module, file.getFileName().toString());
                    classUnit.put(((JCClassDecl) def).sym, cu);
                }
        }
        Names.prepare();

        for( JCCompilationUnit cu : units )
        {
            for( JCTree def : cu.defs )
            {
                if( !(def instanceof JCClassDecl) ) continue;
                JCClassDecl cd = (JCClassDecl) def;
                String module = classModule.get(cd.sym);
                if( module.equals("manual") ) continue;
                StringBuilder sb = moduleText.computeIfAbsent(module, k -> new StringBuilder());
                Emitter em = new Emitter(module, sb);
                em.topClass(cd);
            }
        }
        writeOutputs();
        System.err.println("qh2lua: " + declared.size() + " classes, " + moduleText.size() + " modules, "
            + inventory.size() + " external members, " + unresolved.size() + " unresolved names");
        for( Map.Entry<String, Integer> e : unresolved.entrySet() )
            System.err.println("qh2lua: unresolved " + e.getKey() + " x" + e.getValue());
    }

    static String moduleFor(Path file)
    {
        Path rel = srcRoot.relativize(file);
        List<String> parts = new ArrayList<>();
        for( Path p : rel ) parts.add(p.toString());
        // com/questhelper/...
        List<String> qh = parts.subList(2, parts.size());
        if( qh.size() >= 1 && qh.get(0).equals("panel") )
        {
            // Swing is hand-ported (script/plugins/quest_helper/ui); the panel's data classes are not Swing
            try
            {
                String text = Files.readString(file);
                if( text.contains("import javax.swing") || text.contains("import java.awt") ) return "manual";
            }
            catch( IOException e ) { throw new RuntimeException(e); }
        }
        if( qh.size() >= 3 && qh.get(0).equals("helpers") )
        {
            if( qh.size() == 3 ) // helpers/<cat>/File.java
                return "helpers/" + qh.get(1) + "/" + qh.get(2).replace(".java", "").toLowerCase(Locale.ROOT);
            return "helpers/" + qh.get(1) + "/" + qh.get(2);
        }
        if( qh.size() >= 3 && qh.get(0).equals("playerquests") )
            return "playerquests/" + qh.get(1);
        StringBuilder m = new StringBuilder("fw");
        for( int i = 0; i < qh.size() - 1; i++ ) m.append("/").append(qh.get(i));
        return m.toString();
    }

    static boolean isLazyModule(String module)
    {
        return module.startsWith("helpers/") || module.startsWith("playerquests/");
    }

    static void collect(JCClassDecl cd, String module, String file)
    {
        declared.put(cd.sym, cd);
        classModule.put(cd.sym, module);
        classFile.put(cd.sym, file);
        for( JCTree def : cd.defs )
            if( def instanceof JCClassDecl )
                collect((JCClassDecl) def, module, file);
    }

    /* A class is INTERNAL when its source is ours (emitted or hand-written). */
    static boolean internal(Symbol s)
    {
        ClassSymbol c = outermostClass(s);
        return c != null && declared.containsKey(c) || (s instanceof ClassSymbol && declared.containsKey(s));
    }

    static ClassSymbol outermostClass(Symbol s)
    {
        Symbol c = s;
        while( c != null && !(c instanceof ClassSymbol) ) c = c.owner;
        ClassSymbol last = (ClassSymbol) c;
        while( c != null )
        {
            if( c instanceof ClassSymbol ) last = (ClassSymbol) c;
            c = c.owner;
        }
        return last;
    }

    static String key(ClassSymbol c)
    {
        return c.flatname.toString();
    }

    /* A class whose source is ours but hand-ported (com.questhelper.panel Swing). */
    static boolean manual(Symbol s)
    {
        ClassSymbol c = outermostClass(s);
        return c != null && "manual".equals(classModule.get(c));
    }

    static void inv(String kind, String owner, String lua, String java)
    {
        inventory.merge(kind + "\t" + owner + "\t" + lua + "\t" + java, 1, Integer::sum);
    }

    /* ------------------------------------------------------------------ names */

    static final Set<String> LUA_RESERVED = new HashSet<>(Arrays.asList(
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "global", "goto", "if", "in",
        "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"));
    /* Globals the emitted code itself names; a Java local may not shadow one. */
    static final Set<String> PROTECTED = new HashSet<>(Arrays.asList(
        "self", "J", "C", "math", "string", "table", "utf8", "type", "tostring", "tonumber", "select", "pairs",
        "ipairs", "next", "rawget", "rawset", "rawequal", "setmetatable", "error", "assert", "_ENV", "_G", "api",
        "super", "cls", "_"));

    static String ident(String n)
    {
        n = n.replace("$", "_S");
        if( LUA_RESERVED.contains(n) ) return n + "_";
        return n;
    }

    static String local(String n)
    {
        n = ident(n);
        if( PROTECTED.contains(n) || n.startsWith("__") ) return n + "_";
        return n;
    }

    static class Names
    {
        static final Map<String, Set<String>> internalSigs = new HashMap<>();
        static final Map<MethodSymbol, String> cache = new HashMap<>();
        static final Set<String> methodNames = new HashSet<>();

        static void prepare()
        {
            for( ClassSymbol c : declared.keySet() )
                for( Symbol m : c.members().getSymbols(s -> s.kind == Kinds.Kind.MTH) )
                {
                    MethodSymbol ms = (MethodSymbol) m;
                    if( ms.isConstructor() || ms.name.toString().equals("<clinit>") ) continue;
                    MethodSymbol r = root(ms);
                    if( internal(r.owner) )
                        internalSigs.computeIfAbsent(r.name.toString(), k -> new HashSet<>()).add(sig(r));
                }
            for( ClassSymbol c : declared.keySet() )
            {
                for( Symbol m : c.members().getSymbols(s -> s.kind == Kinds.Kind.MTH) )
                    if( !((MethodSymbol) m).isConstructor() ) methodNames.add(method((MethodSymbol) m));
                for( Type st : types.closure(c.type) )
                    if( !internal(st.tsym) )
                        for( Symbol m : st.tsym.members().getSymbols(s -> s.kind == Kinds.Kind.MTH) )
                            if( !((MethodSymbol) m).isConstructor() ) methodNames.add(method((MethodSymbol) m));
            }
        }

        static MethodSymbol root(MethodSymbol m)
        {
            if( m.isStatic() || m.isConstructor() ) return m;
            ClassSymbol owner = (ClassSymbol) m.owner;
            for( Type st : types.closure(owner.type) )
            {
                if( st.tsym == owner ) continue;
                for( Symbol cand : st.tsym.members().getSymbolsByName(m.name) )
                {
                    if( !(cand instanceof MethodSymbol) ) continue;
                    MethodSymbol c = (MethodSymbol) cand;
                    if( c.isStatic() || (c.flags() & Flags.PRIVATE) != 0 ) continue;
                    if( m.overrides(c, owner, types, false) )
                    {
                        MethodSymbol r = root(c);
                        if( r.owner == syms.objectType.tsym || !internal(r.owner) || internal(r.owner) )
                            return r;
                    }
                }
            }
            return m;
        }

        static String sig(MethodSymbol m)
        {
            StringBuilder sb = new StringBuilder();
            for( VarSymbol p : m.params() )
            {
                if( sb.length() > 0 ) sb.append("_");
                sb.append(simple(types.erasure(p.type)));
            }
            return sb.toString();
        }

        static String simple(Type t)
        {
            if( t.getTag() == TypeTag.ARRAY ) return simple(((Type.ArrayType) t).elemtype) + "Arr";
            if( t.isPrimitive() ) return t.getTag().name().toLowerCase(Locale.ROOT);
            return t.tsym.name.toString().replace("$", "_");
        }

        static boolean libOverloaded(MethodSymbol r)
        {
            Set<String> sigs = new HashSet<>();
            ClassSymbol owner = (ClassSymbol) r.owner;
            for( Type st : types.closure(owner.type) )
                for( Symbol cand : st.tsym.members().getSymbolsByName(r.name) )
                    if( cand instanceof MethodSymbol && !((MethodSymbol) cand).isConstructor() )
                        sigs.add(sig((MethodSymbol) cand));
            return sigs.size() > 1;
        }

        static boolean internalOverloaded(MethodSymbol r)
        {
            Set<String> s = internalSigs.get(r.name.toString());
            if( s != null && s.size() > 1 ) return true;
            ClassSymbol owner = (ClassSymbol) r.owner;
            for( Type st : types.closure(owner.type) )
            {
                if( internal(st.tsym) ) continue;
                for( Symbol cand : st.tsym.members().getSymbolsByName(r.name) )
                    if( cand instanceof MethodSymbol && !((MethodSymbol) cand).isConstructor()
                        && !sig((MethodSymbol) cand).equals(sig(r)) )
                        return true;
            }
            return false;
        }

        static String method(MethodSymbol m)
        {
            String c = cache.get(m);
            if( c != null ) return c;
            MethodSymbol r = root(m);
            String base = ident(r.name.toString());
            boolean over = internal(r.owner) ? internalOverloaded(r) : libOverloaded(r);
            String n = over ? base + "__" + sig(r) : base;
            if( n.startsWith("__") ) n = "m" + n;
            cache.put(m, n);
            return n;
        }

        static String ctor(MethodSymbol m, String prefix)
        {
            ClassSymbol owner = (ClassSymbol) m.owner;
            int count = 0;
            for( Symbol s : owner.members().getSymbolsByName(m.name) )
                if( s instanceof MethodSymbol && ((MethodSymbol) s).isConstructor() ) count++;
            return count <= 1 ? prefix : prefix + "__" + sig(m);
        }

        static String field(VarSymbol v)
        {
            String base = ident(v.name.toString());
            if( internal(v.owner) && (methodNames.contains(base) || base.startsWith("__")) ) base = base + "_";
            // Java fields do not override: a field that hides one a superclass
            // declares is a different slot, so it takes its class's name.
            if( internal(v.owner) && !v.isStatic() && v.owner instanceof ClassSymbol && hidesInherited(v) )
                base = base + "_" + ((ClassSymbol) v.owner).name.toString().replace("$", "_");
            return base;
        }

        static boolean hidesInherited(VarSymbol v)
        {
            Type st = ((ClassSymbol) v.owner).getSuperclass();
            while( st != null && st.getTag() == TypeTag.CLASS )
            {
                for( Symbol s : st.tsym.members().getSymbolsByName(v.name) )
                    if( s instanceof VarSymbol && !s.isStatic() ) return true;
                st = ((ClassSymbol) st.tsym).getSuperclass();
            }
            return false;
        }
    }

    /* ------------------------------------------------------------------ output */

    static void writeOutputs() throws IOException
    {
        Files.createDirectories(outRoot);
        for( Map.Entry<String, StringBuilder> e : moduleText.entrySet() )
        {
            Path p = outRoot.resolve(e.getKey() + ".lua");
            Files.createDirectories(p.getParent());
            String head = "-- GENERATED by tools/questhelper_port/QhToLua.java from the RuneLite Quest Helper. Do not edit.\n"
                + "-- module " + e.getKey() + "\n";
            Files.writeString(p, head + e.getValue().toString());
        }
        // index
        StringBuilder ix = new StringBuilder("-- GENERATED by tools/questhelper_port/QhToLua.java. Do not edit.\n");
        ix.append("return {\n  framework = {\n");
        List<String> fw = moduleText.keySet().stream().filter(m -> !isLazyModule(m)).collect(Collectors.toList());
        for( String m : fw ) ix.append("    ").append(lstr(m)).append(",\n");
        ix.append("  },\n  modules = {\n");
        for( String m : moduleText.keySet() )
        {
            if( !isLazyModule(m) ) continue;
            Set<String> deps = moduleDeps.getOrDefault(m, Collections.emptySet());
            ix.append("    [").append(lstr(m)).append("] = {");
            for( String d : deps ) if( isLazyModule(d) && !d.equals(m) ) ix.append(lstr(d)).append(", ");
            ix.append("},\n");
        }
        ix.append("  },\n  classes = {\n");
        TreeMap<String, String> byKey = new TreeMap<>();
        for( Map.Entry<ClassSymbol, String> e : classModule.entrySet() ) byKey.put(key(e.getKey()), e.getValue());
        for( Map.Entry<String, String> e : byKey.entrySet() )
            if( isLazyModule(e.getValue()) )
                ix.append("    [").append(lstr(e.getKey())).append("] = ").append(lstr(e.getValue())).append(",\n");
        // bases: a lazy class -> its nearest superclass in a framework module,
        // so a J.lazy_new proxy can answer that class's setters and getters
        // (QuestHelper.setQuest/getQuest/getState) without loading its module.
        ix.append("  },\n  bases = {\n");
        TreeMap<String, String> bases = new TreeMap<>();
        for( Map.Entry<ClassSymbol, String> e : classModule.entrySet() )
        {
            if( !isLazyModule(e.getValue()) ) continue;
            ClassSymbol c = e.getKey();
            if( c.isInterface() || c.isEnum() || c.isAnonymous() ) continue;
            Type st = c.getSuperclass();
            while( st != null && st.tsym instanceof ClassSymbol )
            {
                ClassSymbol sc = (ClassSymbol) st.tsym;
                String sm = classModule.get(outermostClass(sc));
                if( sm == null || !isLazyModule(sm) )
                {
                    if( sm != null && !"manual".equals(sm) ) bases.put(key(c), key(sc));
                    break;
                }
                st = sc.getSuperclass();
            }
        }
        for( Map.Entry<String, String> e : bases.entrySet() )
            ix.append("    [").append(lstr(e.getKey())).append("] = ").append(lstr(e.getValue())).append(",\n");
        // overrides: the framework methods a lazy class (or a lazy ancestor)
        // redefines -- a proxy must load the module before answering those.
        ix.append("  },\n  overrides = {\n");
        for( Map.Entry<ClassSymbol, String> e : classModule.entrySet() )
        {
            String bk = bases.get(key(e.getKey()));
            if( bk == null ) continue;
            ClassSymbol c = e.getKey();
            Set<String> fwNames = new HashSet<>();
            Set<String> own = new TreeSet<>();
            Type st = c.type;
            while( st != null && st.tsym instanceof ClassSymbol )
            {
                ClassSymbol sc = (ClassSymbol) st.tsym;
                String sm = classModule.get(outermostClass(sc));
                boolean lazy = sm != null && isLazyModule(sm);
                for( Symbol m : sc.members().getSymbols() )
                {
                    if( !(m instanceof MethodSymbol) || m.isStatic() || m.isConstructor() ) continue;
                    String n = Names.method((MethodSymbol) m);
                    if( lazy ) own.add(n); else fwNames.add(n);
                }
                st = sc.getSuperclass();
            }
            own.retainAll(fwNames);
            if( own.isEmpty() ) continue;
            ix.append("    [").append(lstr(key(c))).append("] = {");
            for( String n : own ) ix.append("[").append(lstr(n)).append("] = true, ");
            ix.append("},\n");
        }
        ix.append("  },\n}\n");
        Files.writeString(outRoot.resolve("index.lua"), ix.toString());

        StringBuilder inv = new StringBuilder("# kind\towner\tlua\tjava\tuses\n");
        for( Map.Entry<String, Integer> e : inventory.entrySet() )
            inv.append(e.getKey()).append("\t").append(e.getValue()).append("\n");
        Files.writeString(outRoot.resolve("inventory.tsv"), inv.toString());
        writeEnums();
    }

    /* RuneLite enums, by reflection on the same classpath javac used. */
    static void writeEnums() throws IOException
    {
        List<URL> urls = new ArrayList<>();
        for( String p : classpath.split(File.pathSeparator) ) urls.add(new File(p).toURI().toURL());
        StringBuilder sb = new StringBuilder("-- GENERATED by tools/questhelper_port/QhToLua.java (reflection over the runelite jars). Do not edit.\n");
        try( URLClassLoader cl = new URLClassLoader(urls.toArray(new URL[0]), null) )
        {
            for( ClassSymbol e : externalEnums )
            {
                Class<?> k;
                try { k = Class.forName(e.flatname.toString(), true, cl); }
                catch( Throwable t ) { System.err.println("qh2lua: enum " + e.flatname + " not loadable: " + t); continue; }
                Object[] consts = k.getEnumConstants();
                if( consts == null ) continue;
                sb.append("J.ext_enum(").append(lstr(e.flatname.toString())).append(", {\n");
                for( Object o : consts ) sb.append("  ").append(lstr(((Enum<?>) o).name())).append(",\n");
                sb.append("}, {\n");
                Method[] methods = k.getDeclaredMethods();
                // reflection order is unspecified: sort, so a rebuild is byte-identical
                Arrays.sort(methods, Comparator.comparing(Method::getName));
                for( Method m : methods )
                {
                    if( Modifier.isStatic(m.getModifiers()) || !Modifier.isPublic(m.getModifiers()) || m.getParameterCount() != 0 ) continue;
                    Class<?> rt = m.getReturnType();
                    if( !(rt.isPrimitive() && rt != void.class) && rt != String.class && !rt.isEnum() && rt != int[].class ) continue;
                    MethodSymbol ms = null;
                    for( Symbol s : e.members().getSymbolsByName(names(m.getName())) )
                        if( s instanceof MethodSymbol && ((MethodSymbol) s).params().isEmpty() ) ms = (MethodSymbol) s;
                    if( ms == null ) continue;
                    String lua = Names.method(ms);
                    sb.append("  [").append(lstr(lua)).append("] = {");
                    for( Object o : consts )
                    {
                        Object v;
                        try { m.setAccessible(true); v = m.invoke(o); } catch( Throwable t ) { v = null; }
                        sb.append(luaValue(v)).append(", ");
                    }
                    sb.append("},\n");
                }
                sb.append("})\n");
            }
        }
        Files.writeString(outRoot.resolve("enums.lua"), sb.toString());
    }

    static com.sun.tools.javac.util.Name names(String s)
    {
        return syms.objectType.tsym.name.table.fromString(s);
    }

    static String luaValue(Object v)
    {
        if( v == null ) return "J.NIL";
        if( v instanceof String ) return lstr((String) v);
        if( v instanceof Boolean ) return v.toString();
        if( v instanceof Character ) return Integer.toString((Character) v);
        if( v instanceof Float || v instanceof Double ) return dbl(((Number) v).doubleValue());
        if( v instanceof Number ) return v.toString();
        if( v instanceof Enum ) return "J.ENUMREF(" + lstr(((Enum<?>) v).getDeclaringClass().getName()) + ", " + lstr(((Enum<?>) v).name()) + ")";
        if( v instanceof int[] )
        {
            int[] a = (int[]) v;
            StringBuilder sb = new StringBuilder("J.arr({");
            for( int x : a ) sb.append(x).append(",");
            return sb.append("}, ").append(a.length).append(")").toString();
        }
        return "J.NIL";
    }

    static String dbl(double d)
    {
        if( Double.isNaN(d) ) return "(0.0/0.0)";
        if( Double.isInfinite(d) ) return d > 0 ? "math.huge" : "(-math.huge)";
        String s = Double.toString(d);
        return s;
    }

    static String lstr(String s)
    {
        StringBuilder sb = new StringBuilder("\"");
        for( int i = 0; i < s.length(); i++ )
        {
            char ch = s.charAt(i);
            int cp = ch;
            if( Character.isHighSurrogate(ch) && i + 1 < s.length() )
            {
                cp = Character.toCodePoint(ch, s.charAt(i + 1));
                i++;
            }
            switch( cp )
            {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if( cp >= 32 && cp < 127 ) sb.append((char) cp);
                    else if( cp < 32 || cp == 127 ) sb.append(String.format("\\x%02X", cp));
                    else sb.append("\\u{").append(Integer.toHexString(cp)).append("}");
            }
        }
        return sb.append("\"").toString();
    }

    /* ================================================================ emitter */

    /** A receiver in scope: the class whose `this` it is and the Lua expression naming it. */
    static class Frame
    {
        final ClassSymbol cls;
        final String expr;
        Frame(ClassSymbol c, String e) { cls = c; expr = e; }
    }

    /** A break/continue target. */
    static class Target
    {
        final JCTree tree;
        final String brk;     // label after the statement
        final String cont;    // label at the continue point, null for a switch/block
        final int boundary;   // try-closure depth it lives at
        final boolean isSwitch;
        boolean usedBreakGoto;
        Target(JCTree t, String b, String c, int d, boolean sw) { tree = t; brk = b; cont = c; boundary = d; isSwitch = sw; }
    }

    static int labelSerial = 0;

    static class Emitter
    {
        final String module;
        final StringBuilder out;
        int indent = 0;
        ClassSymbol cls;                     // class whose body is being emitted
        List<Frame> frames = new ArrayList<>();
        Deque<Target> targets = new ArrayDeque<>();
        int boundary = 0;                    // try-closure depth
        int methodBoundary = 0;
        int tempSerial = 0;
        int selfDepth = 0;
        final Map<Symbol, String> localNames = new HashMap<>();
        final Map<ClassSymbol, String> localClasses = new HashMap<>();
        JCMethodDecl method;
        String currentLabel = null;          // a JCLabeledStatement name for the next loop

        Emitter(String module, StringBuilder out) { this.module = module; this.out = out; }

        void line(String s)
        {
            if( s.startsWith("(") ) s = ";" + s;
            for( int i = 0; i < indent; i++ ) out.append("  ");
            out.append(s).append("\n");
        }

        String temp() { return "__t" + (++tempSerial); }

        void dep(ClassSymbol c)
        {
            if( c == null ) return;
            ClassSymbol o = outermostClass(c);
            String m = classModule.get(o);
            if( m != null && !m.equals(module) )
                moduleDeps.computeIfAbsent(module, k -> new TreeSet<>()).add(m);
        }

        /* ---------------------------------------------------------- classes */

        void topClass(JCClassDecl cd)
        {
            classDef(cd);
        }

        String classRef(ClassSymbol c)
        {
            String lc = localClasses.get(c);
            if( lc != null ) return lc;
            if( internal(c) ) dep(c);
            else invClass(c);
            return "C[" + lstr(key(c)) + "]";
        }

        void invClass(ClassSymbol c)
        {
            inv("class", key(c), "", "");
            if( (c.flags() & Flags.ENUM) != 0 ) externalEnums.add(c);
        }

        String superKey(ClassSymbol c)
        {
            Type st = c.getSuperclass();
            if( st == null || st.getTag() != TypeTag.CLASS ) return "java.lang.Object";
            ClassSymbol s = (ClassSymbol) st.tsym;
            if( internal(s) ) dep(s); else invClass(s);
            return key(s);
        }

        List<String> ifaceKeys(ClassSymbol c)
        {
            List<String> r = new ArrayList<>();
            for( Type t : c.getInterfaces() )
            {
                ClassSymbol s = (ClassSymbol) t.tsym;
                if( internal(s) ) dep(s); else invClass(s);
                r.add(key(s));
            }
            return r;
        }

        /** A J.def for a named (member, top-level or local) class. */
        void classDef(JCClassDecl cd)
        {
            ClassSymbol c = cd.sym;
            boolean isLocal = c.isDirectlyOrIndirectlyLocal() && c.owner.kind != Kinds.Kind.TYP && c.owner.kind != Kinds.Kind.PCK;
            String kind = c.isInterface() ? "interface" : (c.flags() & Flags.ENUM) != 0 ? "enum" : "class";
            StringBuilder meta = new StringBuilder();
            meta.append("kind = ").append(lstr(kind)).append(", super = ").append(lstr(c.isInterface() ? "java.lang.Object" : superKey(c)));
            List<String> ifs = ifaceKeys(c);
            meta.append(", ifaces = {");
            for( String s : ifs ) meta.append(lstr(s)).append(", ");
            meta.append("}");
            if( hasOuter(c) ) meta.append(", outer = true");
            meta.append(", file = ").append(lstr(classFile.getOrDefault(outermostClass(c), "?")));
            if( (c.flags() & Flags.ABSTRACT) != 0 ) meta.append(", abstract = true");

            if( isLocal )
            {
                String ln = "LC_" + local(c.name.toString()) + "_" + (++labelSerial);
                localClasses.put(c, ln);
                line("local " + ln);
                line(ln + " = J.local_class(" + lstr(key(c)) + ", {" + meta + ",");
            }
            else
            {
                line("J.def(" + lstr(key(c)) + ", {" + meta + ",");
            }
            indent++;
            annotationsMeta(cd);
            // body
            ClassSymbol saveCls = cls;
            List<Frame> saveFrames = frames;
            int saveDepth = selfDepth;
            Map<Symbol, String> saveLocals = new HashMap<>(localNames);
            cls = c;
            List<Frame> nf = new ArrayList<>();
            if( isLocal )
            {
                nf.addAll(frames);
                selfDepth++;
            }
            else
            {
                // member inner classes reach outer instances through __outer
                String e = "self";
                ClassSymbol cc = c;
                List<Frame> chain = new ArrayList<>();
                while( hasOuter(cc) )
                {
                    e = e + ".__outer";
                    cc = (ClassSymbol) cc.owner.enclClass();
                    chain.add(0, new Frame(cc, e));
                }
                nf.addAll(chain);
                selfDepth = 0;
            }
            String self = selfName();
            nf.add(new Frame(c, self));
            frames = nf;
            line("body = function(cls, super)");
            indent++;
            classMembers(cd, self, false);
            indent--;
            line("end,");
            staticInit(cd);
            indent--;
            line("})");
            cls = saveCls;
            frames = saveFrames;
            selfDepth = saveDepth;
            localNames.clear();
            localNames.putAll(saveLocals);
            // nested member/static classes
            if( !isLocal )
                for( JCTree def : cd.defs )
                    if( def instanceof JCClassDecl ) classDef((JCClassDecl) def);
        }

        String selfName() { return selfDepth == 0 ? "self" : "self_" + selfDepth; }

        boolean hasOuter(ClassSymbol c)
        {
            return c.hasOuterInstance() && !c.isDirectlyOrIndirectlyLocal() && !c.isAnonymous();
        }

        void annotationsMeta(JCClassDecl cd)
        {
            StringBuilder inject = new StringBuilder();
            StringBuilder sub = new StringBuilder();
            StringBuilder cfg = new StringBuilder();
            String group = annotationValue(cd.mods, "ConfigGroup", "value");
            // Guice scope: one instance per injector tree
            if( hasAnnotation(cd.mods, "Singleton") ) line("singleton = true,");
            for( JCTree def : cd.defs )
            {
                if( def instanceof JCVariableDecl )
                {
                    JCVariableDecl v = (JCVariableDecl) def;
                    if( hasAnnotation(v.mods, "Inject") )
                    {
                        ClassSymbol t = (ClassSymbol) types.erasure(v.sym.type).tsym;
                        if( !internal(t) ) invClass(t);
                        inject.append("{").append(lstr(Names.field(v.sym))).append(", ").append(lstr(key(t)));
                        // @Named("x"): the binding name, which need not be the field's
                        String named = annotationValue(v.mods, "Named", "value");
                        if( named != null ) inject.append(", ").append(lstr(named));
                        inject.append("}, ");
                    }
                    if( hasAnnotation(v.mods, "ConfigSection") )
                    {
                        cfg.append("{section = ").append(lstr(String.valueOf(v.sym.getConstValue())))
                            .append(", name = ").append(lstr(annotationValue(v.mods, "ConfigSection", "name")))
                            .append(", position = ").append(numOr(annotationValue(v.mods, "ConfigSection", "position"), "0"))
                            .append(", closedByDefault = ").append(boolOr(annotationValue(v.mods, "ConfigSection", "closedByDefault")))
                            .append("}, ");
                    }
                }
                if( def instanceof JCMethodDecl )
                {
                    JCMethodDecl m = (JCMethodDecl) def;
                    if( hasAnnotation(m.mods, "Subscribe") )
                    {
                        String pr = annotationValue(m.mods, "Subscribe", "priority");
                        ClassSymbol ev = (ClassSymbol) types.erasure(m.sym.params().head.type).tsym;
                        if( !internal(ev) ) invClass(ev);
                        sub.append("{").append(lstr(Names.method(m.sym))).append(", ").append(lstr(key(ev))).append(", ")
                            .append(pr == null ? "0" : pr.replace("f", "")).append("}, ");
                    }
                    if( hasAnnotation(m.mods, "Inject") && m.sym.isConstructor() )
                    {
                        StringBuilder ps = new StringBuilder();
                        for( VarSymbol p : m.sym.params() )
                        {
                            ClassSymbol t = (ClassSymbol) types.erasure(p.type).tsym;
                            ps.append(lstr(key(t))).append(", ");
                        }
                        line("inject_ctor = {" + lstr(Names.ctor(m.sym, "__init")) + ", {" + ps + "}},");
                    }
                    if( hasAnnotation(m.mods, "ConfigItem") )
                    {
                        Type rt = types.erasure(m.sym.getReturnType());
                        String rk = rt.isPrimitive() ? rt.getTag().name().toLowerCase(Locale.ROOT) : key((ClassSymbol) rt.tsym);
                        if( !rt.isPrimitive() && !internal(rt.tsym) ) invClass((ClassSymbol) rt.tsym);
                        cfg.append("{method = ").append(lstr(Names.method(m.sym)))
                            .append(", keyName = ").append(lstr(annotationValue(m.mods, "ConfigItem", "keyName")))
                            .append(", name = ").append(lstr(nz(annotationValue(m.mods, "ConfigItem", "name"))))
                            .append(", description = ").append(lstr(nz(annotationValue(m.mods, "ConfigItem", "description"))))
                            .append(", position = ").append(numOr(annotationValue(m.mods, "ConfigItem", "position"), "-1"))
                            .append(", section = ").append(sectionRef(m.mods))
                            .append(", hidden = ").append(boolOr(annotationValue(m.mods, "ConfigItem", "hidden")))
                            .append(", type = ").append(lstr(rk));
                        String min = annotationValue(m.mods, "Range", "min");
                        String max = annotationValue(m.mods, "Range", "max");
                        if( min != null ) cfg.append(", min = ").append(min);
                        if( max != null ) cfg.append(", max = ").append(max);
                        if( hasAnnotation(m.mods, "Alpha") ) cfg.append(", alpha = true");
                        cfg.append("}, ");
                    }
                }
            }
            if( inject.length() > 0 ) line("inject = {" + inject + "},");
            if( sub.length() > 0 ) line("subscribe = {" + sub + "},");
            if( cfg.length() > 0 ) line("config = {" + cfg + "},");
            if( group != null ) line("config_group = " + lstr(group) + ",");
        }

        String sectionRef(JCModifiers mods)
        {
            for( JCAnnotation a : mods.annotations )
                if( a.annotationType.toString().endsWith("ConfigItem") )
                    for( JCExpression arg : a.args )
                        if( arg instanceof JCAssign && ((JCAssign) arg).lhs.toString().equals("section") )
                        {
                            JCExpression rhs = ((JCAssign) arg).rhs;
                            Symbol s = TreeInfo.symbol(rhs);
                            if( s instanceof VarSymbol && ((VarSymbol) s).getConstValue() != null )
                                return lstr(String.valueOf(((VarSymbol) s).getConstValue()));
                            if( rhs instanceof JCLiteral ) return lstr(String.valueOf(((JCLiteral) rhs).value));
                        }
            return "nil";
        }

        String nz(String s) { return s == null ? "" : s; }
        String numOr(String s, String d) { return s == null ? d : s; }
        /* javac keeps a boolean constant as Integer 1/0: spell it as Lua's boolean. */
        String boolOr(String s)
        {
            if( s == null || s.equals("0") || s.equals("false") ) return "false";
            if( s.equals("1") || s.equals("true") ) return "true";
            return s;
        }

        boolean hasAnnotation(JCModifiers mods, String simple)
        {
            for( JCAnnotation a : mods.annotations )
            {
                String n = a.annotationType.toString();
                if( n.equals(simple) || n.endsWith("." + simple) ) return true;
            }
            return false;
        }

        String annotationValue(JCModifiers mods, String simple, String attr)
        {
            for( JCAnnotation a : mods.annotations )
            {
                String n = a.annotationType.toString();
                if( !(n.equals(simple) || n.endsWith("." + simple)) ) continue;
                for( JCExpression arg : a.args )
                {
                    JCExpression lhs = null, rhs = arg;
                    if( arg instanceof JCAssign ) { lhs = ((JCAssign) arg).lhs; rhs = ((JCAssign) arg).rhs; }
                    String name = lhs == null ? "value" : lhs.toString();
                    if( !name.equals(attr) ) continue;
                    Symbol s = TreeInfo.symbol(rhs);
                    if( s instanceof VarSymbol && ((VarSymbol) s).getConstValue() != null )
                        return String.valueOf(((VarSymbol) s).getConstValue());
                    if( rhs instanceof JCLiteral ) return String.valueOf(((JCLiteral) rhs).value);
                    if( rhs.type != null && rhs.type.constValue() != null ) return String.valueOf(rhs.type.constValue());
                    return rhs.toString();
                }
            }
            return null;
        }

        /** Methods, constructors, field initialisers of a class body. */
        void classMembers(JCClassDecl cd, String self, boolean anon)
        {
            ClassSymbol c = cd.sym;
            // __zero: primitive defaults
            List<String> zeros = new ArrayList<>();
            List<JCTree> fieldInits = new ArrayList<>();
            for( JCTree def : cd.defs )
            {
                if( def instanceof JCVariableDecl )
                {
                    JCVariableDecl v = (JCVariableDecl) def;
                    if( v.sym.isStatic() ) continue;
                    String d = defaultValue(v.sym.type);
                    if( d != null ) zeros.add(self + "." + Names.field(v.sym) + " = " + d);
                    if( v.init != null ) fieldInits.add(v);
                }
                else if( def instanceof JCBlock && (((JCBlock) def).flags & Flags.STATIC) == 0 )
                    fieldInits.add(def);
            }
            if( !zeros.isEmpty() )
            {
                line("function cls.__zero(" + self + ")");
                indent++;
                line("super.__zero(" + self + ")");
                for( String z : zeros ) line(z);
                indent--;
                line("end");
            }
            line("function cls.__fields(" + self + ")");
            indent++;
            for( JCTree f : fieldInits )
            {
                if( f instanceof JCVariableDecl )
                {
                    JCVariableDecl v = (JCVariableDecl) f;
                    beginMethod(null);
                    String val = assignConv(v.init, v.sym.type);
                    flushPrelude();
                    line(self + "." + Names.field(v.sym) + " = " + val);
                }
                else
                {
                    beginMethod(null);
                    blockStats(((JCBlock) f).stats);
                }
            }
            indent--;
            line("end");
            for( JCTree def : cd.defs )
            {
                if( !(def instanceof JCMethodDecl) ) continue;
                JCMethodDecl m = (JCMethodDecl) def;
                if( m.body == null ) continue;
                if( anon && m.sym.isConstructor() ) continue;
                methodDef(m, self);
            }
        }

        String defaultValue(Type t)
        {
            switch( t.getTag() )
            {
                case BOOLEAN: return "false";
                case INT: case LONG: case SHORT: case BYTE: case CHAR: return "0";
                case FLOAT: case DOUBLE: return "0.0";
                default: return null;
            }
        }

        void staticInit(JCClassDecl cd)
        {
            ClassSymbol c = cd.sym;
            List<JCTree> inits = new ArrayList<>();
            List<JCVariableDecl> enumConsts = new ArrayList<>();
            for( JCTree def : cd.defs )
            {
                if( def instanceof JCVariableDecl )
                {
                    JCVariableDecl v = (JCVariableDecl) def;
                    if( !v.sym.isStatic() ) continue;
                    if( (v.sym.flags() & Flags.ENUM) != 0 ) { enumConsts.add(v); continue; }
                    inits.add(v);
                }
                else if( def instanceof JCBlock && (((JCBlock) def).flags & Flags.STATIC) != 0 )
                    inits.add(def);
            }
            if( inits.isEmpty() && enumConsts.isEmpty() ) return;
            line("clinit = function(cls)");
            indent++;
            List<Frame> saveFrames = frames;
            frames = new ArrayList<>();
            int ord = 0;
            for( JCVariableDecl v : enumConsts )
            {
                beginMethod(null);
                JCNewClass nc = (JCNewClass) v.init;
                MethodSymbol ctor = (MethodSymbol) nc.constructor;
                String args = argList(nc.args, ctor, nc.varargsElement);
                String name = lstr(v.sym.name.toString());
                String fname = Names.field(v.sym);
                if( nc.def != null )
                {
                    String fn = anonBody(nc.def, true);
                    flushPrelude();
                    line("cls." + fname + " = J.enum_new_anon(cls, " + name + ", " + ord + ", " + fn + ", "
                        + lstr(Names.ctor(ctor, "__init")) + (args.isEmpty() ? "" : ", " + args) + ")");
                }
                else
                {
                    flushPrelude();
                    line("cls." + fname + " = J.enum_new(cls, " + name + ", " + ord + ", "
                        + lstr(Names.ctor(ctor, "__init")) + (args.isEmpty() ? "" : ", " + args) + ")");
                }
                ord++;
            }
            if( !enumConsts.isEmpty() )
            {
                StringBuilder vs = new StringBuilder();
                for( JCVariableDecl v : enumConsts ) vs.append("cls.").append(Names.field(v.sym)).append(", ");
                line("cls.__values = J.arr({" + vs + "}, " + enumConsts.size() + ")");
            }
            for( JCTree t : inits )
            {
                beginMethod(null);
                if( t instanceof JCVariableDecl )
                {
                    JCVariableDecl v = (JCVariableDecl) t;
                    if( v.init == null )
                    {
                        String d = defaultValue(v.sym.type);
                        if( d != null ) line("cls." + Names.field(v.sym) + " = " + d);
                        continue;
                    }
                    String val = assignConv(v.init, v.sym.type);
                    flushPrelude();
                    line("cls." + Names.field(v.sym) + " = " + val);
                }
                else blockStats(((JCBlock) t).stats);
            }
            frames = saveFrames;
            indent--;
            line("end,");
        }

        void beginMethod(JCMethodDecl m)
        {
            method = m;
            boundary = 0;
            methodBoundary = 0;
            targets.clear();
        }

        void methodDef(JCMethodDecl m, String self)
        {
            MethodSymbol ms = m.sym;
            JCMethodDecl saveMethod = method;
            int saveB = boundary, saveMB = methodBoundary;
            Deque<Target> saveT = targets;
            targets = new ArrayDeque<>();
            beginMethod(m);
            List<String> ps = new ArrayList<>();
            for( JCVariableDecl p : m.params )
            {
                String n = local(p.name.toString());
                localNames.put(p.sym, n);
                ps.add(n);
            }
            line("-- " + classFile.getOrDefault(outermostClass(cls), "?") + ":" + lineOf(m) + " " + ms.name);
            if( ms.isConstructor() )
            {
                String init = Names.ctor(ms, "__init");
                List<String> all = new ArrayList<>();
                all.add(self);
                if( hasOuter(cls) ) all.add("__outer");
                all.addAll(ps);
                line("function cls." + init + "(" + String.join(", ", all) + ")");
                indent++;
                if( hasOuter(cls) ) line(self + ".__outer = __outer");
                com.sun.tools.javac.util.List<JCStatement> stats = m.body.stats;
                boolean delegatesThis = false;
                JCStatement first = stats.isEmpty() ? null : stats.head;
                JCMethodInvocation call = null;
                if( first instanceof JCExpressionStatement && ((JCExpressionStatement) first).expr instanceof JCMethodInvocation )
                {
                    JCMethodInvocation mi = (JCMethodInvocation) ((JCExpressionStatement) first).expr;
                    String n = TreeInfo.name(mi.meth).toString();
                    if( n.equals("this") || n.equals("super") ) call = mi;
                }
                boolean isEnum = (cls.flags() & Flags.ENUM) != 0;
                if( call != null )
                {
                    MethodSymbol target = (MethodSymbol) TreeInfo.symbol(call.meth);
                    String args = argList(call.args, target, call.varargsElement);
                    String tn = TreeInfo.name(call.meth).toString();
                    ClassSymbol tc = (ClassSymbol) target.owner;
                    String recv = tn.equals("this") ? "cls" : "super";
                    String outerArg = "";
                    if( tn.equals("this") ) { delegatesThis = true; if( hasOuter(cls) ) outerArg = "__outer"; }
                    else if( hasOuter(tc) ) outerArg = outerFor(tc, call.meth);
                    flushPrelude();
                    if( !(isEnum && tn.equals("super")) )
                    {
                        String nm = internal(tc) ? Names.ctor(target, "__init") : extCtor(target, "__init");
                        line(recv + "." + nm + "(" + self + (outerArg.isEmpty() ? "" : ", " + outerArg)
                            + (args.isEmpty() ? "" : ", " + args) + ")");
                    }
                    stats = stats.tail;
                }
                else if( !isEnum )
                {
                    // implicit super()
                    ClassSymbol sc = (ClassSymbol) cls.getSuperclass().tsym;
                    MethodSymbol sctor = noArgCtor(sc);
                    String nm = sctor == null ? "__init" : (internal(sc) ? Names.ctor(sctor, "__init") : extCtor(sctor, "__init"));
                    String outerArg = hasOuter(sc) ? outerFor(sc, null) : "";
                    line("super." + nm + "(" + self + (outerArg.isEmpty() ? "" : ", " + outerArg) + ")");
                }
                if( !delegatesThis ) line("cls.__fields(" + self + ")");
                blockStats(stats);
                indent--;
                line("end");
            }
            else
            {
                String name = Names.method(ms);
                if( ms.isStatic() )
                    line("function cls." + name + "(" + String.join(", ", ps) + ")");
                else
                {
                    List<String> all = new ArrayList<>();
                    all.add(self);
                    all.addAll(ps);
                    line("function cls." + name + "(" + String.join(", ", all) + ")");
                }
                indent++;
                List<Frame> saveFrames = frames;
                if( ms.isStatic() ) frames = new ArrayList<>(frames.subList(0, Math.max(0, frames.size() - 1)));
                blockStats(m.body.stats);
                frames = saveFrames;
                indent--;
                line("end");
            }
            method = saveMethod;
            boundary = saveB;
            methodBoundary = saveMB;
            targets = saveT;
        }

        MethodSymbol noArgCtor(ClassSymbol c)
        {
            if( c == null ) return null;
            for( Symbol s : c.members().getSymbolsByName(names("<init>")) )
                if( s instanceof MethodSymbol && ((MethodSymbol) s).params().isEmpty() ) return (MethodSymbol) s;
            for( Symbol s : c.members().getSymbolsByName(names("<init>")) )
                if( s instanceof MethodSymbol && ((MethodSymbol) s).isVarArgs() && ((MethodSymbol) s).params().size() == 1 ) return (MethodSymbol) s;
            return null;
        }

        /** The Lua expression for the outer instance a new inner `target` needs. */
        String outerFor(ClassSymbol target, JCTree site)
        {
            ClassSymbol need = (ClassSymbol) target.owner.enclClass();
            if( site instanceof JCFieldAccess )
            {
                JCFieldAccess fa = (JCFieldAccess) site;
                if( !fa.name.toString().equals("super") && !fa.name.toString().equals("this") )
                    return expr(fa.selected);
            }
            return receiverFor(need);
        }

        String receiverFor(Symbol owner)
        {
            for( int i = frames.size() - 1; i >= 0; i-- )
            {
                Frame f = frames.get(i);
                if( f.cls == owner || f.cls.isSubClass(owner, types) ) return f.expr;
            }
            for( int i = frames.size() - 1; i >= 0; i-- )
                if( frames.get(i).cls.isEnclosedBy((ClassSymbol) owner) ) return frames.get(i).expr;
            return frames.isEmpty() ? "self" : frames.get(frames.size() - 1).expr;
        }

        String extCtor(MethodSymbol ctor, String prefix)
        {
            ClassSymbol owner = (ClassSymbol) ctor.owner;
            String n = Names.ctor(ctor, prefix);
            inv("ctor", key(owner), n.replace("__init", "__new"), owner.name + "(" + Names.sig(ctor) + ")");
            return n;
        }

        int lineOf(JCTree t)
        {
            JCCompilationUnit cu = classUnit.get(outermostClass(cls));
            return cu == null ? t.pos : (int) cu.lineMap.getLineNumber(t.pos);
        }

        /* ---------------------------------------------------------- statements */

        StringBuilder prelude = null;

        void flushPrelude() { }

        /** Emit statements; expressions write their hoisted parts straight to `out`. */
        void blockStats(List<JCStatement> stats)
        {
            for( JCStatement s : stats ) stmt(s);
        }

        void stmt(JCStatement s)
        {
            switch( s.getTag() )
            {
                case BLOCK:
                    line("do");
                    indent++;
                    blockStats(((JCBlock) s).stats);
                    indent--;
                    line("end");
                    break;
                case VARDEF: varDef((JCVariableDecl) s); break;
                case EXEC: exprStmt(((JCExpressionStatement) s).expr); break;
                case IF: ifStmt((JCIf) s, false); break;
                case WHILELOOP: whileStmt((JCWhileLoop) s); break;
                case DOLOOP: doStmt((JCDoWhileLoop) s); break;
                case FORLOOP: forStmt((JCForLoop) s); break;
                case FOREACHLOOP: foreachStmt((JCEnhancedForLoop) s); break;
                case LABELLED: labelled((JCLabeledStatement) s); break;
                case SWITCH: switchStmt((JCSwitch) s); break;
                case RETURN: returnStmt((JCReturn) s); break;
                case BREAK: breakStmt((JCBreak) s); break;
                case CONTINUE: continueStmt((JCContinue) s); break;
                case THROW:
                {
                    String e = sub(((JCThrow) s).expr);
                    line("J.throw(" + e + ")");
                    break;
                }
                case TRY: tryStmt((JCTry) s); break;
                case SYNCHRONIZED:
                    line("do");
                    indent++;
                    blockStats(((JCSynchronized) s).body.stats);
                    indent--;
                    line("end");
                    break;
                case SKIP: break;
                case ASSERT: break;
                case CLASSDEF: classDef((JCClassDecl) s); break;
                default:
                    throw new RuntimeException("statement " + s.getTag() + " at " + where(s));
            }
        }

        String where(JCTree t)
        {
            return classFile.getOrDefault(outermostClass(cls), "?") + ":" + lineOf(t);
        }

        /** Evaluate an expression in a nested statement context: preludes go to out. */
        String sub(JCExpression e) { return expr(e); }

        void varDef(JCVariableDecl v)
        {
            String n = local(v.name.toString());
            if( localNames.containsValue(n) && !localNames.containsKey(v.sym) )
            {
                // Java forbids shadowing a local, but a lambda/anon body may reuse a name
            }
            if( v.init == null )
            {
                localNames.put(v.sym, n);
                line("local " + n);
                return;
            }
            int mark = out.length();
            String val = assignConv(v.init, v.sym.type);
            boolean hoisted = out.length() != mark;
            localNames.put(v.sym, n);
            if( hoisted )
            {
                String pre = out.substring(mark);
                out.setLength(mark);
                line("local " + n);
                line("do");
                indent++;
                for( String l : pre.split("\n") ) out.append("  ").append(l).append("\n");
                line(n + " = " + val);
                indent--;
                line("end");
            }
            else line("local " + n + " = " + val);
        }

        void exprStmt(JCExpression e)
        {
            switch( e.getTag() )
            {
                case ASSIGN:
                {
                    JCAssign a = (JCAssign) e;
                    assign(a.lhs, a.rhs, null, false);
                    return;
                }
                case PREINC: case POSTINC: case PREDEC: case POSTDEC:
                {
                    JCUnary u = (JCUnary) e;
                    incdec(u, false);
                    return;
                }
                default:
                    if( e instanceof JCAssignOp )
                    {
                        JCAssignOp a = (JCAssignOp) e;
                        assign(a.lhs, a.rhs, a, false);
                        return;
                    }
            }
            String x = expr(e);
            if( x.endsWith(")") && !x.startsWith("(") ) line(x);
            else line("local _ = " + x);
        }

        void ifStmt(JCIf s, boolean asElseIf)
        {
            String c = cond(s.cond);
            line((asElseIf ? "elseif " : "if ") + c + " then");
            indent++;
            stmtInBlock(s.thenpart);
            indent--;
            if( s.elsepart != null )
            {
                if( s.elsepart instanceof JCIf && !hasPrelude(((JCIf) s.elsepart).cond) )
                {
                    ifStmt((JCIf) s.elsepart, true);
                    return;
                }
                line("else");
                indent++;
                stmtInBlock(s.elsepart);
                indent--;
            }
            line("end");
        }

        boolean hasPrelude(JCExpression e)
        {
            int mark = out.length();
            int saveTemp = tempSerial;
            int saveLabel = labelSerial;
            Map<ClassSymbol, String> saveLC = new HashMap<>(localClasses);
            expr(e);
            boolean h = out.length() != mark;
            out.setLength(mark);
            tempSerial = saveTemp;
            labelSerial = saveLabel;
            localClasses.clear();
            localClasses.putAll(saveLC);
            return h;
        }

        String cond(JCExpression e)
        {
            return stripParens(expr(e));
        }

        String stripParens(String s) { return s; }

        void stmtInBlock(JCStatement s)
        {
            if( s instanceof JCBlock ) blockStats(((JCBlock) s).stats);
            else stmt(s);
        }

        String takeLabel()
        {
            String l = currentLabel;
            currentLabel = null;
            return l;
        }

        Target pushLoop(JCTree t)
        {
            int n = ++labelSerial;
            String l = takeLabel();
            Target tg = new Target(t, "brk_" + n, "cont_" + n, boundary, false);
            tg.usedBreakGoto = l != null;
            targets.push(tg);
            return tg;
        }

        void whileStmt(JCWhileLoop s)
        {
            Target tg = pushLoop(s);
            if( hasPrelude(s.cond) )
            {
                line("while true do");
                indent++;
                String c = cond(s.cond);
                line("if not (" + c + ") then break end");
            }
            else
            {
                line("while " + cond(s.cond) + " do");
                indent++;
            }
            loopBody(s.body, tg);
            indent--;
            line("end");
            targets.pop();
            line("::" + tg.brk + "::");
        }

        void loopBody(JCStatement body, Target tg)
        {
            line("do");
            indent++;
            stmtInBlock(body);
            indent--;
            line("end");
            line("::" + tg.cont + "::");
        }

        void doStmt(JCDoWhileLoop s)
        {
            Target tg = pushLoop(s);
            line("repeat");
            indent++;
            loopBody(s.body, tg);
            String t = temp();
            line("local " + t);
            line("do");
            indent++;
            String c = cond(s.cond);
            line(t + " = " + c);
            indent--;
            line("end");
            indent--;
            line("until not " + t);
            targets.pop();
            line("::" + tg.brk + "::");
        }

        void forStmt(JCForLoop s)
        {
            line("do");
            indent++;
            for( JCStatement i : s.init ) stmt(i);
            Target tg = pushLoop(s);
            if( s.cond == null )
            {
                line("while true do");
                indent++;
            }
            else if( hasPrelude(s.cond) )
            {
                line("while true do");
                indent++;
                String c = cond(s.cond);
                line("if not (" + c + ") then break end");
            }
            else
            {
                line("while " + cond(s.cond) + " do");
                indent++;
            }
            loopBody(s.body, tg);
            for( JCExpressionStatement st : s.step ) stmt(st);
            indent--;
            line("end");
            targets.pop();
            line("::" + tg.brk + "::");
            indent--;
            line("end");
        }

        void foreachStmt(JCEnhancedForLoop s)
        {
            Target tg = pushLoop(s);
            String n = local(s.var.name.toString());
            localNames.put(s.var.sym, n);
            Type t = types.erasure(s.expr.type);
            String coll = expr(s.expr);
            if( t.getTag() == TypeTag.ARRAY )
            {
                String a = temp(), i = temp();
                line("local " + a + " = " + coll);
                line("for " + i + " = 1, " + a + ".n do");
                indent++;
                line("local " + n + " = " + a + "[" + i + "]");
            }
            else
            {
                String k = temp(), v = temp();
                line("for " + k + ", " + v + " in J.iter(" + coll + ") do");
                indent++;
                line("local " + n + " = " + v);
            }
            loopBody(s.body, tg);
            indent--;
            line("end");
            targets.pop();
            line("::" + tg.brk + "::");
        }

        void labelled(JCLabeledStatement s)
        {
            JCStatement body = s.body;
            if( body instanceof JCWhileLoop || body instanceof JCForLoop || body instanceof JCEnhancedForLoop || body instanceof JCDoWhileLoop )
            {
                currentLabel = s.label.toString();
                labelOf.put(body, s);
                stmt(body);
                return;
            }
            int n = ++labelSerial;
            Target tg = new Target(s, "brk_" + n, null, boundary, false);
            targets.push(tg);
            labelOf.put(s, s);
            line("do");
            indent++;
            stmt(body);
            indent--;
            line("end");
            targets.pop();
            line("::" + tg.brk + "::");
        }

        final Map<JCTree, JCLabeledStatement> labelOf = new HashMap<>();

        Target findTarget(JCTree target, boolean isContinue)
        {
            for( Target t : targets )
            {
                if( t.tree == target ) return t;
                JCLabeledStatement ls = labelOf.get(t.tree);
                if( ls != null && ls == target ) return t;
                if( target instanceof JCLabeledStatement && ((JCLabeledStatement) target).body == t.tree ) return t;
            }
            throw new RuntimeException("no jump target at " + where(target));
        }

        void switchStmt(JCSwitch s)
        {
            int n = ++labelSerial;
            String end = "sw_end_" + n;
            Target tg = new Target(s, end, null, boundary, true);
            line("do");
            indent++;
            String sel = temp();
            String sv = expr(s.selector);
            line("local " + sel + " = " + sv);
            boolean enumSel = (types.erasure(s.selector.type).tsym.flags() & Flags.ENUM) != 0;
            List<String> conds = new ArrayList<>();
            String defLabel = null;
            int ci = 0;
            for( JCCase c : s.cases )
            {
                String lbl = "sw_" + n + "_" + (ci++);
                List<String> parts = new ArrayList<>();
                boolean isDefault = false;
                for( JCCaseLabel cl : c.labels )
                {
                    if( cl instanceof JCDefaultCaseLabel ) { isDefault = true; continue; }
                    JCExpression e = ((JCConstantCaseLabel) cl).expr;
                    String v;
                    if( enumSel && e instanceof JCIdent )
                    {
                        Symbol es = ((JCIdent) e).sym;
                        v = classRef((ClassSymbol) es.owner) + "." + Names.field((VarSymbol) es);
                    }
                    else v = expr(e);
                    parts.add(sel + " == " + v);
                }
                if( isDefault ) defLabel = lbl;
                if( !parts.isEmpty() ) conds.add(String.join(" or ", parts) + "\u0000" + lbl);
            }
            boolean first = true;
            for( String cnd : conds )
            {
                String[] p = cnd.split("\u0000");
                line((first ? "if " : "elseif ") + p[0] + " then goto " + p[1]);
                first = false;
            }
            if( first ) line("goto " + (defLabel != null ? defLabel : end));
            else
            {
                line("else goto " + (defLabel != null ? defLabel : end));
                line("end");
            }
            targets.push(tg);
            ci = 0;
            for( JCCase c : s.cases )
            {
                String lbl = "sw_" + n + "_" + (ci++);
                line("::" + lbl + "::");
                line("do");
                indent++;
                blockStats(c.stats);
                indent--;
                line("end");
            }
            targets.pop();
            line("::" + end + "::");
            indent--;
            line("end");
        }

        void returnStmt(JCReturn s)
        {
            Type rt = method != null ? method.sym.getReturnType() : (lambdaReturn != null ? lambdaReturn.type : null);
            String v = s.expr == null ? null : assignConv(s.expr, rt);
            if( boundary > methodBoundary )
                line("do return 1" + (v == null ? "" : ", " + v) + " end");
            else
                line("do return" + (v == null ? "" : " " + v) + " end");
        }

        void breakStmt(JCBreak s)
        {
            Target t = findTarget(s.target, false);
            if( t.boundary < boundary )
            {
                line("do return 2, " + lstr(t.brk) + " end");
                return;
            }
            if( t.isSwitch || t.usedBreakGoto || t.cont == null ) line("goto " + t.brk);
            else
            {
                // innermost loop between here and the target? a plain break only exits the innermost
                Target inner = innermostLoop();
                if( inner == t ) line("goto " + t.brk);
                else line("goto " + t.brk);
            }
        }

        Target innermostLoop()
        {
            for( Target t : targets ) if( t.cont != null ) return t;
            return null;
        }

        void continueStmt(JCContinue s)
        {
            Target t = findTarget(s.target, true);
            if( t.boundary < boundary )
            {
                line("do return 3, " + lstr(t.cont) + " end");
                return;
            }
            line("goto " + t.cont);
        }

        void tryStmt(JCTry s)
        {
            // try-with-resources: resources are locals closed in the finally
            List<String> resources = new ArrayList<>();
            line("do");
            indent++;
            for( JCTree r : s.resources )
            {
                if( r instanceof JCVariableDecl )
                {
                    varDef((JCVariableDecl) r);
                    resources.add(localNames.get(((JCVariableDecl) r).sym));
                }
            }
            String st = temp(), rv = temp();
            boundary++;
            line("local " + st + ", " + rv + " = J.try(function()");
            indent++;
            blockStats(s.body.stats);
            indent--;
            line("end, " + (s.catchers.isEmpty() ? "nil" : "function(__e)"));
            if( !s.catchers.isEmpty() )
            {
                indent++;
                boolean first = true;
                for( JCCatch c : s.catchers )
                {
                    List<String> tests = new ArrayList<>();
                    List<JCExpression> alts = new ArrayList<>();
                    if( c.param.vartype instanceof JCTypeUnion ) alts.addAll(((JCTypeUnion) c.param.vartype).alternatives);
                    else alts.add(c.param.vartype);
                    for( JCExpression a : alts )
                    {
                        ClassSymbol ec = (ClassSymbol) types.erasure(a.type).tsym;
                        tests.add("J.catches(__e, " + lstr(key(ec)) + ")");
                        if( !internal(ec) ) invClass(ec);
                    }
                    line((first ? "if " : "elseif ") + String.join(" or ", tests) + " then");
                    first = false;
                    indent++;
                    String n = local(c.param.name.toString());
                    localNames.put(c.param.sym, n);
                    line("local " + n + " = J.exception(__e)");
                    blockStats(c.body.stats);
                    indent--;
                }
                line("else");
                line("  J.rethrow(__e)");
                line("end");
                indent--;
            }
            boolean hasFinally = s.finalizer != null || !resources.isEmpty();
            if( hasFinally )
            {
                line("end, function()");
                indent++;
                for( int i = resources.size() - 1; i >= 0; i-- )
                    line("if " + resources.get(i) + " then " + resources.get(i) + ":close() end");
                if( s.finalizer != null ) blockStats(s.finalizer.stats);
                indent--;
                line("end)");
            }
            else line(s.catchers.isEmpty() ? ")" : "end)");
            boundary--;
            // propagate jumps out of the closure
            line("if " + st + " == 1 then");
            indent++;
            if( boundary > methodBoundary ) line("do return 1, " + rv + " end");
            else line("do return " + rv + " end");
            indent--;
            line("end");
            boolean any = false;
            for( Target t : targets )
            {
                if( boundary > t.boundary )
                {
                    line("if (" + st + " == 2 and " + rv + " == " + lstr(t.brk) + ") then do return 2, " + rv + " end end");
                    if( t.cont != null ) line("if (" + st + " == 3 and " + rv + " == " + lstr(t.cont) + ") then do return 3, " + rv + " end end");
                }
                else
                {
                    line("if " + st + " == 2 and " + rv + " == " + lstr(t.brk) + " then goto " + t.brk + " end");
                    if( t.cont != null ) line("if " + st + " == 3 and " + rv + " == " + lstr(t.cont) + " then goto " + t.cont + " end");
                }
                any = true;
            }
            indent--;
            line("end");
        }

        /* ---------------------------------------------------------- assignments */

        /** An lvalue: how to read it and how to write it. */
        class LV
        {
            String read;
            String writePrefix; // "x = " style target
        }

        LV lvalue(JCExpression lhs)
        {
            LV lv = new LV();
            lhs = TreeInfo.skipParens(lhs);
            if( lhs instanceof JCIdent )
            {
                Symbol s = ((JCIdent) lhs).sym;
                if( s.kind == Kinds.Kind.VAR && (s.owner.kind == Kinds.Kind.MTH || s.owner.kind == Kinds.Kind.VAR || localNames.containsKey(s)) )
                {
                    lv.read = localNames.getOrDefault(s, local(s.name.toString()));
                }
                else if( s.isStatic() )
                {
                    lv.read = staticField((VarSymbol) s);
                }
                else
                {
                    lv.read = receiverFor(s.owner) + "." + fieldRef((VarSymbol) s);
                }
                lv.writePrefix = lv.read;
                return lv;
            }
            if( lhs instanceof JCFieldAccess )
            {
                JCFieldAccess fa = (JCFieldAccess) lhs;
                Symbol s = fa.sym;
                if( s.isStatic() ) { lv.read = staticField((VarSymbol) s); lv.writePrefix = lv.read; return lv; }
                String obj = selectedReceiver(fa);
                if( !isSimple(obj) )
                {
                    String t = temp();
                    line("local " + t + " = " + obj);
                    obj = t;
                }
                lv.read = obj + "." + fieldRef((VarSymbol) s);
                lv.writePrefix = lv.read;
                return lv;
            }
            if( lhs instanceof JCArrayAccess )
            {
                JCArrayAccess aa = (JCArrayAccess) lhs;
                String a = expr(aa.indexed);
                String i = expr(aa.index);
                if( !isSimple(a) ) { String t = temp(); line("local " + t + " = " + a); a = t; }
                if( !isSimple(i) ) { String t = temp(); line("local " + t + " = " + i); i = t; }
                lv.read = a + "[" + plusOne(i) + "]";
                lv.writePrefix = lv.read;
                return lv;
            }
            throw new RuntimeException("lvalue " + lhs.getTag() + " at " + where(lhs));
        }

        String selectedReceiver(JCFieldAccess fa)
        {
            String sel = fa.selected.toString();
            if( sel.equals("this") || sel.endsWith(".this") )
            {
                if( sel.equals("this") ) return receiverFor(cls);
                return receiverFor(TreeInfo.symbol(((JCFieldAccess) fa.selected).selected));
            }
            if( sel.equals("super") ) return receiverFor(cls);
            return expr(fa.selected);
        }

        boolean isSimple(String s)
        {
            return s.matches("[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)*") || s.matches("-?[0-9]+");
        }

        String plusOne(String i)
        {
            if( i.matches("-?[0-9]+") ) return Long.toString(Long.parseLong(i) + 1);
            return i + " + 1";
        }

        /** Statement or expression assignment. Returns the value expression when asExpr. */
        String assign(JCExpression lhs, JCExpression rhs, JCAssignOp op, boolean asExpr)
        {
            LV lv = lvalue(lhs);
            String val;
            if( op == null ) val = assignConv(rhs, lhs.type);
            else
            {
                /* A compound assignment's own type is the variable's; the
                 * OPERATOR's result is what may need narrowing back to it:
                 * `int x; x /= aFloat` is `x = (int) (x / aFloat)` (JLS
                 * 15.26.2), and passing op.type (int) here skipped the J.d2i,
                 * so x held a float (QuestPerspective.getMinimapPoint). */
                MethodSymbol operator = (MethodSymbol) op.operator;
                Type result = operator != null ? operator.type.getReturnType() : op.type;
                val = binaryOp(op.getTag().noAssignOp(), lv.read, lhs.type, rhs, result, lhs.type, operator);
            }
            if( asExpr )
            {
                String t = temp();
                line("local " + t + " = " + val);
                line(lv.writePrefix + " = " + t);
                return t;
            }
            line(lv.writePrefix + " = " + val);
            return null;
        }

        String incdec(JCUnary u, boolean asExpr)
        {
            LV lv = lvalue(u.arg);
            boolean inc = u.getTag() == JCTree.Tag.PREINC || u.getTag() == JCTree.Tag.POSTINC;
            boolean post = u.getTag() == JCTree.Tag.POSTINC || u.getTag() == JCTree.Tag.POSTDEC;
            String op = inc ? " + 1" : " - 1";
            if( !asExpr )
            {
                line(lv.writePrefix + " = " + lv.read + op);
                return null;
            }
            String t = temp();
            if( post )
            {
                line("local " + t + " = " + lv.read);
                line(lv.writePrefix + " = " + t + op);
            }
            else
            {
                line("local " + t + " = " + lv.read + op);
                line(lv.writePrefix + " = " + t);
            }
            return t;
        }

        /** Assignment conversion: the value of `e` stored in a slot of type `target`. */
        String assignConv(JCExpression e, Type target)
        {
            String v = expr(e);
            if( target != null && (target.getTag() == TypeTag.DOUBLE || target.getTag() == TypeTag.FLOAT)
                && e.type != null && isIntegral(e.type) )
            {
                if( v.matches("-?[0-9]+") ) return v + ".0";
                return "(" + v + " + 0.0)";
            }
            return v;
        }

        boolean isIntegral(Type t)
        {
            Type u = types.unboxedTypeOrType(t);
            switch( u.getTag() )
            {
                case INT: case LONG: case SHORT: case BYTE: case CHAR: return true;
                default: return false;
            }
        }

        boolean isFloating(Type t)
        {
            Type u = types.unboxedTypeOrType(t);
            return u.getTag() == TypeTag.DOUBLE || u.getTag() == TypeTag.FLOAT;
        }

        boolean isString(Type t)
        {
            return t != null && types.isSameType(types.erasure(t), syms.stringType);
        }

        /* ---------------------------------------------------------- expressions */

        String expr(JCExpression e)
        {
            switch( e.getTag() )
            {
                case LITERAL: return literal((JCLiteral) e);
                case PARENS: return "(" + expr(((JCParens) e).expr) + ")";
                case IDENT: return ident((JCIdent) e);
                case SELECT: return select((JCFieldAccess) e);
                case APPLY: return apply((JCMethodInvocation) e);
                case NEWCLASS: return newClass((JCNewClass) e);
                case NEWARRAY: return newArray((JCNewArray) e);
                case ASSIGN: { JCAssign a = (JCAssign) e; return assign(a.lhs, a.rhs, null, true); }
                case PREINC: case PREDEC: case POSTINC: case POSTDEC: return incdec((JCUnary) e, true);
                case NOT: return "(not " + expr(((JCUnary) e).arg) + ")";
                case NEG: return "(-" + expr(((JCUnary) e).arg) + ")";
                case POS: return expr(((JCUnary) e).arg);
                case COMPL: return "(~" + expr(((JCUnary) e).arg) + ")";
                case TYPECAST: return cast((JCTypeCast) e);
                case TYPETEST: return instanceOf((JCInstanceOf) e);
                case INDEXED:
                {
                    JCArrayAccess aa = (JCArrayAccess) e;
                    String a = expr(aa.indexed);
                    String i = expr(aa.index);
                    return a + "[" + plusOne(i) + "]";
                }
                case CONDEXPR: return conditional((JCConditional) e);
                case LAMBDA: return lambda((JCLambda) e);
                case REFERENCE: return memberRef((JCMemberReference) e);
                case AND: case OR: return logical((JCBinary) e);
                default:
                    if( e instanceof JCAssignOp ) { JCAssignOp a = (JCAssignOp) e; return assign(a.lhs, a.rhs, a, true); }
                    if( e instanceof JCBinary )
                    {
                        JCBinary b = (JCBinary) e;
                        return binary(b);
                    }
                    if( e instanceof JCSwitchExpression ) throw new RuntimeException("switch expression at " + where(e));
                    throw new RuntimeException("expression " + e.getTag() + " at " + where(e));
            }
        }

        String literal(JCLiteral l)
        {
            switch( l.typetag )
            {
                case INT: case LONG: case SHORT: case BYTE: return String.valueOf(l.value);
                case CHAR: return Integer.toString((Character) l.getValue());
                case FLOAT: case DOUBLE: return dbl(((Number) l.value).doubleValue());
                case BOOLEAN: return ((Integer) l.value) != 0 ? "true" : "false";
                case BOT: return "nil";
                case CLASS: return lstr((String) l.value);
                default: throw new RuntimeException("literal " + l.typetag);
            }
        }

        String constant(Object v, Type t)
        {
            if( v instanceof String ) return lstr((String) v);
            if( t != null && t.getTag() == TypeTag.BOOLEAN ) return ((Number) v).intValue() != 0 ? "true" : "false";
            if( v instanceof Boolean ) return v.toString();
            if( v instanceof Character ) return Integer.toString((Character) v);
            if( t != null && t.getTag() == TypeTag.CHAR && v instanceof Number ) return Integer.toString(((Number) v).intValue());
            if( v instanceof Float || v instanceof Double ) return dbl(((Number) v).doubleValue());
            return String.valueOf(v);
        }

        String ident(JCIdent id)
        {
            Symbol s = id.sym;
            String nm = id.name.toString();
            if( nm.equals("this") || nm.equals("super") ) return receiverFor(cls);
            if( s == null ) throw new RuntimeException("unresolved ident " + id + " at " + where(id));
            if( s.kind == Kinds.Kind.VAR )
            {
                VarSymbol v = (VarSymbol) s;
                String ln = localNames.get(v);
                if( ln != null ) return ln;
                if( v.getConstValue() != null && v.isStatic() ) return constant(v.getConstValue(), v.type);
                if( v.owner.kind == Kinds.Kind.MTH || v.owner.kind == Kinds.Kind.VAR ) return local(v.name.toString());
                if( v.isStatic() ) return staticField(v);
                return receiverFor(v.owner) + "." + fieldRef(v);
            }
            if( s.kind == Kinds.Kind.TYP ) return classRef((ClassSymbol) s);
            if( s.name.toString().equals("this") ) return receiverFor(cls);
            if( s.kind == Kinds.Kind.ERR ) return unresolvedName(id.toString(), id);
            throw new RuntimeException("ident kind " + s.kind + " " + id + " at " + where(id));
        }

        String unresolvedName(String text, JCTree t)
        {
            String a = aliases.get(text);
            if( a != null ) return a;
            unresolved.merge(text, 1, Integer::sum);
            return "J.unresolved(" + lstr(text) + ")";
        }

        String fieldRef(VarSymbol v)
        {
            if( !internal(v.owner) )
                inv("field", key((ClassSymbol) v.owner), Names.field(v), v.name.toString());
            return Names.field(v);
        }

        String staticField(VarSymbol v)
        {
            if( v.getConstValue() != null ) return constant(v.getConstValue(), v.type);
            ClassSymbol owner = (ClassSymbol) v.owner;
            if( !internal(owner) )
            {
                if( (v.flags() & Flags.ENUM) != 0 )
                {
                    externalEnums.add(owner);
                    inv("enum", key(owner), v.name.toString(), "");
                }
                else inv("sfield", key(owner), Names.field(v), v.name.toString());
            }
            return classRef(owner) + "." + Names.field(v);
        }

        String select(JCFieldAccess fa)
        {
            Symbol s = fa.sym;
            String n = fa.name.toString();
            if( s == null || s.kind == Kinds.Kind.ERR )
                return unresolvedName(fa.toString().replaceAll("^.*\\.(\\w+\\.\\w+)$", "$1"), fa);
            if( n.equals("class") )
            {
                Type t = types.erasure(fa.selected.type);
                if( t.isPrimitive() ) return lstr(t.toString());
                ClassSymbol c = (ClassSymbol) t.tsym;
                if( internal(c) ) dep(c); else invClass(c);
                return "J.classof(" + lstr(key(c)) + ")";
            }
            if( n.equals("this") ) return receiverFor(TreeInfo.symbol(fa.selected));
            if( s.kind == Kinds.Kind.VAR )
            {
                VarSymbol v = (VarSymbol) s;
                if( n.equals("length") && fa.selected.type != null && fa.selected.type.getTag() == TypeTag.ARRAY )
                    return expr(fa.selected) + ".n";
                if( v.isStatic() )
                {
                    return staticField(v);
                }
                return selectedReceiver(fa) + "." + fieldRef(v);
            }
            if( s.kind == Kinds.Kind.TYP ) return classRef((ClassSymbol) s);
            if( s.kind == Kinds.Kind.PCK ) return "nil";
            throw new RuntimeException("select " + s.kind + " " + fa + " at " + where(fa));
        }

        String argList(List<JCExpression> args, MethodSymbol m, Type varargsElement)
        {
            List<String> out = new ArrayList<>();
            List<VarSymbol> params = m == null ? Collections.emptyList() : m.params();
            int np = params.size();
            int i = 0;
            List<String> va = new ArrayList<>();
            for( JCExpression a : args )
            {
                Type pt = null;
                if( m != null )
                {
                    if( varargsElement != null && i >= np - 1 ) pt = varargsElement;
                    else if( i < np ) pt = params.get(i).type;
                }
                String v = assignConv(a, pt);
                if( varargsElement != null && i >= np - 1 ) va.add(v);
                else out.add(v);
                i++;
            }
            if( varargsElement != null )
                out.add("J.arr({" + String.join(", ", va) + "}, " + va.size() + ")");
            else if( m != null && m.isVarArgs() && args.size() == np - 1 )
                // javac leaves varargsElement unset when an argument did not
                // attribute (an unresolved gameval); the empty tail is still owed
                out.add("J.arr({}, 0)");
            return String.join(", ", out);
        }

        String apply(JCMethodInvocation mi)
        {
            Symbol sym = TreeInfo.symbol(mi.meth);
            if( !(sym instanceof MethodSymbol) )
                throw new RuntimeException("call to unresolved " + mi.meth + " at " + where(mi));
            MethodSymbol m = (MethodSymbol) sym;
            String args = argList(mi.args, m, mi.varargsElement);
            JCExpression meth = mi.meth;
            if( meth instanceof JCIdent )
            {
                if( m.isStatic() ) return staticCall(m, args);
                String recv = receiverFor(m.owner);
                return instanceCall(m, recv, (Type) null, args, mi);
            }
            JCFieldAccess fa = (JCFieldAccess) meth;
            String sel = fa.selected.toString();
            if( sel.equals("super") || sel.endsWith(".super") )
            {
                ClassSymbol owner = (ClassSymbol) m.owner;
                String nm = Names.method(m);
                if( !internal(owner) ) inv("method", key(owner), nm, m.name + "(" + Names.sig(m) + ")");
                String recv = receiverFor(cls);
                String sup;
                if( owner.isInterface() && sel.endsWith(".super") ) sup = classRef(owner);
                else sup = classRef((ClassSymbol) frameClass(recv).getSuperclass().tsym);
                return sup + "." + nm + "(" + recv + (args.isEmpty() ? "" : ", " + args) + ")";
            }
            if( m.isStatic() )
            {
                // evaluate a non-type receiver for its side effects? Java code rarely does.
                return staticCall(m, args);
            }
            String recv = selectedReceiver(fa);
            return instanceCall(m, recv, fa.selected.type, args, mi);
        }

        ClassSymbol frameClass(String recv)
        {
            for( int i = frames.size() - 1; i >= 0; i-- ) if( frames.get(i).expr.equals(recv) ) return frames.get(i).cls;
            return cls;
        }

        String staticCall(MethodSymbol m, String args)
        {
            ClassSymbol owner = (ClassSymbol) m.owner;
            String nm = Names.method(m);
            if( !internal(owner) ) inv("static", key(owner), nm, m.name + "(" + Names.sig(m) + ")");
            else if( manual(owner) ) inv("manual-static", key(owner), nm, m.name + "(" + Names.sig(m) + ")");
            return classRef(owner) + "." + nm + "(" + args + ")";
        }

        String instanceCall(MethodSymbol m, String recv, Type recvType, String args, JCTree site)
        {
            ClassSymbol owner = (ClassSymbol) m.owner;
            String nm = Names.method(m);
            MethodSymbol root = Names.root(m);
            ClassSymbol rootOwner = (ClassSymbol) root.owner;
            if( !internal(rootOwner) ) inv("method", key(rootOwner), nm, m.name + "(" + Names.sig(m) + ")");
            else if( manual(owner) || manual(rootOwner) ) inv("manual-method", key(manual(owner) ? owner : rootOwner), nm, m.name + "(" + Names.sig(m) + ")");
            Type rt = recvType == null ? null : types.erasure(recvType);
            if( rt != null && rt.getTag() == TypeTag.ARRAY )
            {
                if( m.name.toString().equals("clone") ) return "J.aclone(" + recv + ")";
                return "J.vcall(" + recv + ", " + lstr(nm) + (args.isEmpty() ? "" : ", " + args) + ")";
            }
            boolean dynamic = false;
            if( rt != null )
            {
                String k = rt.tsym.flatName().toString();
                if( isBoxed(k) || k.equals("java.lang.Object") || k.equals("java.lang.Number") || k.equals("java.lang.Comparable")
                    || k.equals("java.io.Serializable") || rt.getTag() == TypeTag.TYPEVAR )
                    dynamic = true;
            }
            else if( rootOwner == syms.objectType.tsym && !internal(owner) ) dynamic = false;
            if( dynamic )
            {
                if( rt != null && isBoxed(rt.tsym.flatName().toString()) )
                    return "C[" + lstr(rt.tsym.flatName().toString()) + "]." + nm + "(" + recv + (args.isEmpty() ? "" : ", " + args) + ")";
                return "J.vcall(" + recv + ", " + lstr(nm) + (args.isEmpty() ? "" : ", " + args) + ")";
            }
            if( !isSimple(recv) && !recv.endsWith(")") && !recv.endsWith("]") ) recv = "(" + recv + ")";
            if( recv.matches("-?[0-9].*") || recv.startsWith("\"") ) recv = "(" + recv + ")";
            return recv + ":" + nm + "(" + args + ")";
        }

        boolean isBoxed(String k)
        {
            switch( k )
            {
                case "java.lang.Integer": case "java.lang.Long": case "java.lang.Double": case "java.lang.Float":
                case "java.lang.Boolean": case "java.lang.Character": case "java.lang.Short": case "java.lang.Byte":
                    return true;
                default: return false;
            }
        }

        String newClass(JCNewClass nc)
        {
            MethodSymbol ctor = (MethodSymbol) nc.constructor;
            if( ctor == null ) throw new RuntimeException("unresolved constructor at " + where(nc));
            String args = argList(nc.args, ctor, nc.varargsElement);
            if( nc.def != null ) return anonNew(nc, ctor, args);
            ClassSymbol c = (ClassSymbol) ctor.owner;
            String lc = localClasses.get(c);
            if( lc != null ) return lc + "." + Names.ctor(ctor, "__new") + "(" + args + ")";
            if( internal(c) )
            {
                String nm = Names.ctor(ctor, "__new");
                String m = classModule.get(outermostClass(c));
                String outer = "";
                if( hasOuter(c) ) outer = nc.encl != null ? expr(nc.encl) : receiverFor(c.owner.enclClass());
                // framework -> quest: a lazy proxy (the quest loads on demand).
                // quest -> another quest's class: a module dependency instead
                // (index.modules), so loading this quest loads that one first.
                if( m != null && isLazyModule(m) && !m.equals(module) && !isLazyModule(module) )
                {
                    return "J.lazy_new(" + lstr(key(c)) + ", " + lstr(nm) + (outer.isEmpty() ? "" : ", " + outer)
                        + (args.isEmpty() ? "" : ", " + args) + ")";
                }
                dep(c);
                if( manual(c) ) inv("manual-ctor", key(c), nm, c.name + "(" + Names.sig(ctor) + ")");
                return classRef(c) + "." + nm + "(" + (outer.isEmpty() ? "" : outer + (args.isEmpty() ? "" : ", ")) + args + ")";
            }
            String nm = Names.ctor(ctor, "__new");
            inv("ctor", key(c), nm, c.name + "(" + Names.sig(ctor) + ")");
            return classRef(c) + "." + nm + "(" + args + ")";
        }

        /** `new Base(args) { body }` -> J.anon(...) */
        String anonNew(JCNewClass nc, MethodSymbol ctor, String args)
        {
            JCClassDecl def = nc.def;
            ClassSymbol ac = def.sym;
            Type st = ac.getSuperclass();
            ClassSymbol base = (ClassSymbol) st.tsym;
            List<Type> ifs = ac.getInterfaces();
            String baseRef = classRef(base);
            StringBuilder ifr = new StringBuilder();
            for( Type t : ifs ) { ClassSymbol ic = (ClassSymbol) t.tsym; if( internal(ic) ) dep(ic); else invClass(ic); ifr.append(lstr(key(ic))).append(", "); }
            // the anonymous class's own constructor forwards to a superclass constructor
            MethodSymbol superCtor = null;
            for( JCTree d : def.defs )
                if( d instanceof JCMethodDecl && ((JCMethodDecl) d).sym.isConstructor() )
                {
                    JCMethodDecl md = (JCMethodDecl) d;
                    for( JCStatement s : md.body.stats )
                        if( s instanceof JCExpressionStatement && ((JCExpressionStatement) s).expr instanceof JCMethodInvocation )
                        {
                            Symbol t = TreeInfo.symbol(((JCMethodInvocation) ((JCExpressionStatement) s).expr).meth);
                            if( t instanceof MethodSymbol && ((MethodSymbol) t).isConstructor() ) superCtor = (MethodSymbol) t;
                        }
                }
            if( superCtor == null ) superCtor = base.isInterface() ? null : noArgCtor(base);
            String init = superCtor == null ? "nil" : lstr(internal(base) ? Names.ctor(superCtor, "__init") : extCtor(superCtor, "__init"));
            String outer = hasOuter(base) ? (nc.encl != null ? expr(nc.encl) : receiverFor(base.owner.enclClass())) : "";
            String fn = anonBody(def, false);
            return "J.anon(" + baseRef + ", {" + ifr + "}, " + fn + ", " + init
                + (outer.isEmpty() ? ", nil" : ", " + outer) + (args.isEmpty() ? "" : ", " + args) + ")";
        }

        /** The body function of an anonymous class, written inline. */
        String anonBody(JCClassDecl def, boolean enumBody)
        {
            ClassSymbol saveCls = cls;
            List<Frame> saveFrames = frames;
            int saveDepth = selfDepth;
            JCMethodDecl saveMethod = method;
            int saveB = boundary, saveMB = methodBoundary;
            Deque<Target> saveT = targets;
            cls = def.sym;
            selfDepth = enumBody ? 0 : selfDepth + 1;
            String self = selfName();
            frames = new ArrayList<>(enumBody ? Collections.emptyList() : frames);
            frames.add(new Frame(def.sym, self));
            StringBuilder save = new StringBuilder(out);
            int mark = out.length();
            int saveIndent = indent;
            indent = indent + 1;
            line("function(cls, super)");
            indent++;
            classMembers(def, self, true);
            indent--;
            for( int i = 0; i < indent; i++ ) out.append("  ");
            out.append("end");
            String body = out.substring(mark);
            out.setLength(mark);
            indent = saveIndent;
            cls = saveCls;
            frames = saveFrames;
            selfDepth = saveDepth;
            method = saveMethod;
            boundary = saveB;
            methodBoundary = saveMB;
            targets = saveT;
            return "\n" + body.replaceFirst("^\\s+", "");
        }

        String newArray(JCNewArray na)
        {
            if( na.elems != null )
            {
                List<String> vs = new ArrayList<>();
                Type et = na.type != null && na.type.getTag() == TypeTag.ARRAY ? ((Type.ArrayType) na.type).elemtype : null;
                for( JCExpression e : na.elems ) vs.add(assignConv(e, et));
                return "J.arr({" + String.join(", ", vs) + "}, " + vs.size() + ")";
            }
            Type t = na.type;
            List<String> dims = new ArrayList<>();
            for( JCExpression d : na.dims ) dims.add(expr(d));
            Type et = t;
            for( int i = 0; i < dims.size(); i++ ) et = ((Type.ArrayType) et).elemtype;
            String dv = defaultValue(et);
            if( dims.size() == 1 ) return "J.newarr(" + dims.get(0) + ", " + (dv == null ? "nil" : dv) + ")";
            return "J.newarrN({" + String.join(", ", dims) + "}, " + (dv == null ? "nil" : dv) + ")";
        }

        String cast(JCTypeCast c)
        {
            String v = expr(c.expr);
            Type to = c.clazz.type;
            Type from = c.expr.type;
            if( to == null || from == null ) return v;
            if( !to.isPrimitive() ) return v;
            Type uf = types.unboxedTypeOrType(from);
            switch( to.getTag() )
            {
                case INT: case SHORT: case BYTE: case CHAR: case LONG:
                    if( isFloating(uf) ) v = "J.d2i(" + v + ")";
                    if( to.getTag() == TypeTag.INT && uf.getTag() == TypeTag.LONG ) return "J.l2i(" + v + ")";
                    if( to.getTag() == TypeTag.CHAR && uf.getTag() != TypeTag.CHAR ) return "((" + v + ") & 0xFFFF)";
                    if( to.getTag() == TypeTag.BYTE ) return "J.i2b(" + v + ")";
                    if( to.getTag() == TypeTag.SHORT ) return "J.i2s(" + v + ")";
                    return v;
                case DOUBLE: case FLOAT:
                    if( isIntegral(uf) ) return "(" + v + " + 0.0)";
                    return v;
                default: return v;
            }
        }

        String instanceOf(JCInstanceOf io)
        {
            String v = expr(io.expr);
            Type t = types.erasure(io.pattern.type);
            if( t.getTag() == TypeTag.ARRAY ) return "J.isarray(" + v + ")";
            ClassSymbol c = (ClassSymbol) t.tsym;
            if( internal(c) ) dep(c); else invClass(c);
            return "J.inst(" + v + ", " + lstr(key(c)) + ")";
        }

        boolean alwaysTruthy(JCExpression e)
        {
            e = TreeInfo.skipParens(e);
            Type t = e.type;
            if( t != null && t.isPrimitive() && t.getTag() != TypeTag.BOOLEAN ) return true;
            if( e instanceof JCLiteral ) return ((JCLiteral) e).typetag != TypeTag.BOT && !(((JCLiteral) e).typetag == TypeTag.BOOLEAN && ((Integer) ((JCLiteral) e).value) == 0);
            if( e instanceof JCNewClass || e instanceof JCNewArray || e instanceof JCLambda || e instanceof JCMemberReference ) return true;
            if( e instanceof JCBinary && e.getTag() == JCTree.Tag.PLUS && isString(e.type) ) return true;
            return false;
        }

        /** A ternary branch Lua may evaluate whether or not it is taken: a
         *  literal, a name, `this.f`, a static field, or a cast of one. A call,
         *  an index, a field of another object (null there throws) or
         *  arithmetic (/ by zero) is not. */
        boolean eagerSafe(JCExpression e)
        {
            e = TreeInfo.skipParens(e);
            if( e instanceof JCLiteral || e instanceof JCIdent ) return true;
            if( e instanceof JCTypeCast ) return eagerSafe(((JCTypeCast) e).expr);
            if( e instanceof JCFieldAccess )
            {
                JCFieldAccess f = (JCFieldAccess) e;
                if( f.sym == null || f.sym.kind != com.sun.tools.javac.code.Kinds.Kind.VAR ) return false;
                if( f.sym.isStatic() ) return true;
                JCExpression sel = TreeInfo.skipParens(f.selected);
                return sel instanceof JCIdent && ((JCIdent) sel).name.contentEquals("this");
            }
            return false;
        }

        String conditional(JCConditional c)
        {
            String cv = expr(c.cond);
            int mark = out.length();
            String a = expr(c.truepart);
            String preA = out.substring(mark);
            out.setLength(mark);
            String b = expr(c.falsepart);
            String preB = out.substring(mark);
            out.setLength(mark);
            if( c.type != null && (c.type.getTag() == TypeTag.DOUBLE || c.type.getTag() == TypeTag.FLOAT) )
            {
                if( isIntegral(c.truepart.type) ) a = "(" + a + " + 0.0)";
                if( isIntegral(c.falsepart.type) ) b = "(" + b + " + 0.0)";
            }
            /* J.cond(c, a, b) evaluates BOTH branches before the call: right
             * only when the branch that is not taken cannot fail or act. `s instanceof D ? ((D) s).m() : null`
             * called m on a step that has no m (ConditionalStep.makeWidgetOverlayHint,
             * live: Lumbridge Elite's widget overlay failed every frame). A
             * branch that is more than a plain read is hoisted, as a branch with
             * a prelude already is. */
            if( preA.isEmpty() && preB.isEmpty() )
            {
                /* `c and a or b` with an always-truthy a is lazy as written. */
                if( alwaysTruthy(c.truepart) ) return "(" + cv + " and " + a + " or " + b + ")";
                if( eagerSafe(c.truepart) && eagerSafe(c.falsepart) ) return "J.cond(" + cv + ", " + a + ", " + b + ")";
            }
            // hoisted branches: an if statement into a temp
            String t = temp();
            line("local " + t);
            line("if " + cv + " then");
            out.append(indentLines(preA, 1));
            line("  " + t + " = " + a);
            line("else");
            out.append(indentLines(preB, 1));
            line("  " + t + " = " + b);
            line("end");
            return t;
        }

        String indentLines(String pre, int n)
        {
            if( pre.isEmpty() ) return "";
            StringBuilder sb = new StringBuilder();
            for( String l : pre.split("\n") )
            {
                for( int i = 0; i < n; i++ ) sb.append("  ");
                sb.append(l).append("\n");
            }
            return sb.toString();
        }

        String logical(JCBinary b)
        {
            String l = expr(b.lhs);
            int mark = out.length();
            String r = expr(b.rhs);
            String pre = out.substring(mark);
            out.setLength(mark);
            String op = b.getTag() == JCTree.Tag.AND ? " and " : " or ";
            if( pre.isEmpty() ) return "(" + l + op + r + ")";
            String t = temp();
            line("local " + t + " = " + l);
            line(b.getTag() == JCTree.Tag.AND ? "if " + t + " then" : "if not " + t + " then");
            out.append(indentLines(pre, 1));
            line("  " + t + " = " + r);
            line("end");
            return t;
        }

        String binary(JCBinary b)
        {
            String l = expr(b.lhs);
            return binaryOp(b.getTag(), l, b.lhs.type, b.rhs, b.type, null, (MethodSymbol) b.operator);
        }

        /** lhs is already evaluated (its Lua text); rhs is evaluated here, after it. */
        String binaryOp(JCTree.Tag tag, String l, Type lt, JCExpression rhsTree, Type resultType, Type storeType, MethodSymbol operator)
        {
            String r = expr(rhsTree);
            Type rt = rhsTree.type;
            Type opType = operator != null ? operator.type.getParameterTypes().head : lt;
            boolean strCat = tag == JCTree.Tag.PLUS && (isString(resultType) || isString(lt) || isString(rt));
            if( strCat ) return "(" + strPart(l, lt) + " .. " + strPart(r, rt) + ")";
            boolean integral = opType != null && isIntegral(opType);
            boolean bool = opType != null && types.unboxedTypeOrType(opType).getTag() == TypeTag.BOOLEAN;
            boolean isLong = opType != null && types.unboxedTypeOrType(opType).getTag() == TypeTag.LONG;
            String res;
            switch( tag )
            {
                case PLUS: res = "(" + l + " + " + r + ")"; break;
                case MINUS: res = "(" + l + " - " + r + ")"; break;
                case MUL: res = "(" + l + " * " + r + ")"; break;
                case DIV: res = integral ? "J.idiv(" + l + ", " + r + ")" : "(" + l + " / " + r + ")"; break;
                case MOD: res = "math.fmod(" + l + ", " + r + ")"; break;
                case EQ: res = "(" + l + " == " + r + ")"; break;
                case NE: res = "(" + l + " ~= " + r + ")"; break;
                case LT: res = "(" + l + " < " + r + ")"; break;
                case GT: res = "(" + l + " > " + r + ")"; break;
                case LE: res = "(" + l + " <= " + r + ")"; break;
                case GE: res = "(" + l + " >= " + r + ")"; break;
                case BITAND: res = bool ? "J.band(" + l + ", " + r + ")" : "(" + l + " & " + r + ")"; break;
                case BITOR: res = bool ? "J.bor(" + l + ", " + r + ")" : "(" + l + " | " + r + ")"; break;
                case BITXOR: res = bool ? "(" + l + " ~= " + r + ")" : "(" + l + " ~ " + r + ")"; break;
                case SL: res = isLong ? "(" + l + " << " + r + ")" : "J.shl32(" + l + ", " + r + ")"; break;
                case SR: res = "J.sar(" + l + ", " + r + ")"; break;
                case USR: res = isLong ? "J.ushr64(" + l + ", " + r + ")" : "J.ushr32(" + l + ", " + r + ")"; break;
                default: throw new RuntimeException("binary " + tag);
            }
            if( storeType != null && isIntegral(storeType) && resultType != null && isFloating(resultType) )
                res = "J.d2i(" + res + ")";
            return res;
        }

        String strPart(String v, Type t)
        {
            if( v.startsWith("\"") && v.endsWith("\"") && !v.contains("\" ..") ) return v;
            if( v.startsWith("(") && v.contains(" .. ") ) return v;
            if( t == null ) return "J.str(" + v + ")";
            Type u = types.unboxedTypeOrType(t);
            if( t.isPrimitive() )
            {
                switch( t.getTag() )
                {
                    case CHAR: return "utf8.char(" + v + ")";
                    case INT: case LONG: case SHORT: case BYTE: return "tostring(" + v + ")";
                    case DOUBLE: case FLOAT: return "J.dstr(" + v + ")";
                    case BOOLEAN: return "tostring(" + v + ")";
                    default: break;
                }
            }
            if( isString(t) ) return "J.sval(" + v + ")";
            return "J.str(" + v + ")";
        }

        /* ---------------------------------------------------------- lambdas */

        String samName(Type target)
        {
            MethodSymbol sam = (MethodSymbol) types.findDescriptorSymbol(target.tsym);
            String nm = Names.method(sam);
            ClassSymbol o = (ClassSymbol) Names.root(sam).owner;
            if( !internal(o) ) inv("method", key(o), nm, sam.name + "(" + Names.sig(sam) + ")");
            return nm;
        }

        String ifaceRef(Type target)
        {
            ClassSymbol c = (ClassSymbol) types.erasure(target).tsym;
            if( internal(c) ) dep(c); else invClass(c);
            return lstr(key(c));
        }

        boolean samVoid(Type target)
        {
            Type dt = types.findDescriptorType(target);
            return dt.getReturnType().getTag() == TypeTag.VOID;
        }

        String lambda(JCLambda l)
        {
            Type target = l.type;
            String sam = samName(target);
            List<String> ps = new ArrayList<>();
            ps.add("_");
            for( JCVariableDecl p : l.params )
            {
                String n = local(p.name.toString());
                localNames.put(p.sym, n);
                ps.add(n);
            }
            int saveB = boundary, saveMB = methodBoundary;
            Deque<Target> saveT = targets;
            JCMethodDecl saveMethod = method;
            targets = new ArrayDeque<>();
            boundary = 0;
            methodBoundary = 0;
            int mark = out.length();
            int saveIndent = indent;
            indent = indent + 1;
            line("function(" + String.join(", ", ps) + ")");
            indent++;
            boolean isVoid = samVoid(target);
            LambdaReturn saveLR = lambdaReturn;
            lambdaReturn = new LambdaReturn(types.findDescriptorType(target).getReturnType());
            method = null;
            if( l.getBodyKind() == JCLambda.BodyKind.EXPRESSION )
            {
                JCExpression be = (JCExpression) l.body;
                if( isVoid ) exprStmt(be);
                else
                {
                    String v = assignConv(be, lambdaReturn.type);
                    line("return " + v);
                }
            }
            else
            {
                blockStats(((JCBlock) l.body).stats);
            }
            lambdaReturn = saveLR;
            indent--;
            for( int i = 0; i < indent; i++ ) out.append("  ");
            out.append("end");
            String fn = out.substring(mark);
            out.setLength(mark);
            indent = saveIndent;
            targets = saveT;
            boundary = saveB;
            methodBoundary = saveMB;
            method = saveMethod;
            return "J.fn(" + ifaceRef(target) + ", " + lstr(sam) + ", \n" + fn.replaceFirst("^\\s+", "") + ")";
        }

        class LambdaReturn { Type type; LambdaReturn(Type t) { type = t; } }
        LambdaReturn lambdaReturn = null;

        String memberRef(JCMemberReference r)
        {
            Type target = r.type;
            String sam = samName(target);
            Type dt = types.findDescriptorType(target);
            int n = dt.getParameterTypes().size();
            boolean isVoid = dt.getReturnType().getTag() == TypeTag.VOID;
            List<String> ps = new ArrayList<>();
            for( int i = 0; i < n; i++ ) ps.add("__a" + i);
            MethodSymbol m = (MethodSymbol) r.sym;
            String call;
            switch( r.kind )
            {
                case STATIC:
                {
                    ClassSymbol owner = (ClassSymbol) m.owner;
                    String nm = Names.method(m);
                    if( !internal(owner) ) inv("static", key(owner), nm, m.name + "(" + Names.sig(m) + ")");
                    call = classRef(owner) + "." + nm + "(" + String.join(", ", varargsFix(ps, m)) + ")";
                    break;
                }
                case BOUND:
                {
                    String recv;
                    String sel = r.expr.toString();
                    if( sel.equals("this") ) recv = receiverFor(cls);
                    else if( sel.equals("super") )
                    {
                        String self = receiverFor(cls);
                        String nm = Names.method(m);
                        call = classRef((ClassSymbol) cls.getSuperclass().tsym) + "." + nm + "(" + self + (ps.isEmpty() ? "" : ", " + String.join(", ", ps)) + ")";
                        break;
                    }
                    else
                    {
                        recv = expr(r.expr);
                        if( !isSimple(recv) ) { String t = temp(); line("local " + t + " = " + recv); recv = t; }
                    }
                    call = instanceCall(m, recv, r.expr.type, String.join(", ", ps), r);
                    break;
                }
                case UNBOUND:
                {
                    String recv = ps.get(0);
                    List<String> rest = ps.subList(1, ps.size());
                    call = instanceCall(m, recv, r.expr.type, String.join(", ", rest), r);
                    break;
                }
                case SUPER:
                {
                    String self = receiverFor(cls);
                    call = classRef((ClassSymbol) cls.getSuperclass().tsym) + "." + Names.method(m) + "(" + self + (ps.isEmpty() ? "" : ", " + String.join(", ", ps)) + ")";
                    break;
                }
                case IMPLICIT_INNER:
                case TOPLEVEL:
                {
                    ClassSymbol c = (ClassSymbol) m.owner;
                    String nm = internal(c) ? Names.ctor(m, "__new") : extCtor(m, "__init").replace("__init", "__new");
                    if( internal(c) ) dep(c);
                    String lc = localClasses.get(c);
                    call = (lc != null ? lc : classRef(c)) + "." + nm + "(" + String.join(", ", ps) + ")";
                    break;
                }
                case ARRAY_CTOR:
                {
                    Type et = ((Type.ArrayType) types.erasure(r.expr.type)).elemtype;
                    String dv = defaultValue(et);
                    call = "J.newarr(" + ps.get(0) + ", " + (dv == null ? "nil" : dv) + ")";
                    break;
                }
                default: throw new RuntimeException("member ref " + r.kind + " at " + where(r));
            }
            List<String> all = new ArrayList<>();
            all.add("_");
            all.addAll(ps);
            return "J.fn(" + ifaceRef(target) + ", " + lstr(sam) + ", function(" + String.join(", ", all) + ") "
                + (isVoid ? "" : "return ") + call + " end)";
        }

        List<String> varargsFix(List<String> ps, MethodSymbol m)
        {
            return ps;
        }
    }
}
