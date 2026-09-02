package com.torirs.client;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

/**
 * Reading and writing the handful of manifest keys that vary per deployment.
 *
 * <h2>Why only some keys</h2>
 *
 * A boot manifest has dozens of keys, and almost all of them describe the
 * WORLD -- its revision, its cache format, its interface tree, its lighting.
 * Those belong in the tree and should not be edited on a phone. What genuinely
 * differs between one person's setup and another's is *where the server is*
 * and *where the cache is*, and that is what this edits.
 *
 * The list is deliberately short for a second reason: this is not a manifest
 * editor, it is a way to fix the field that just broke. When the server's DHCP
 * lease moved from .236 to .146, every profile pointing at it was wrong and
 * there was no way to correct that without a desktop and a USB cable. That is
 * the problem this solves.
 *
 * <h2>Why the write is line-oriented</h2>
 *
 * Saving rewrites only the lines whose key it recognises, in place, and copies
 * every other byte through untouched -- comments, section order, blank lines,
 * inline RevConfig, all of it. A parse-and-regenerate editor would silently
 * reformat a file that the client, the desktop build and the repo all share,
 * and would drop anything it did not model. Rewriting one line cannot.
 */
final class ProfileEditor
{
    /**
     * The renderer is not a key -- it is an argv token.
     *
     * `[client:args]` is a lower-priority command line the manifest carries
     * (see src/bootmanifest/bootmanifest.h), and `--soft3d` / `--gles2` are
     * how the client is told which renderer to use (`--gles2-zbuffer` is the
     * depth-buffered world pass of the same renderer). There is deliberately no
     * `renderer=` key to edit: the flag IS the interface, and inventing a
     * second spelling for it here would be a second thing to keep true.
     *
     * So this one field reads and writes repeated `arg=` lines rather than a
     * single key, which is why it is handled apart from the others.
     */
    static final String[] RENDERER_ARGS = { "", "--soft3d", "--gles2", "--gles2-zbuffer" };
    static final String[] RENDERER_LABELS = {
        "(unset - client default)",
        "Software rasterizer",
        "GPU (GLES2, painter order)",
        "GPU (GLES2, depth buffer)"
    };

    /** One editable key: which section it lives in, and how to label it. */
    static final class Field
    {
        final String section;
        final String key;
        final String label;
        final String hint;
        /** Current value, or "" when the key is absent. */
        String value;
        /** The key was present in the file. Absent keys are only written back
         *  when the user actually types something, so saving an untouched
         *  profile cannot add keys it never had. */
        boolean present;

        Field(String section, String key, String label, String hint)
        {
            this.section = section;
            this.key = key;
            this.label = label;
            this.hint = hint;
            this.value = "";
        }
    }

    /**
     * The editable set.
     *
     * `host`/`port` are the game server. `ws_host`/`ws_port` are where the
     * jag archives and login CRCs live, which for LostCity is the same machine
     * on a different port -- and is a separate key precisely because they are
     * not always the same host. `dir` is the cache location.
     */
    static List<Field> fields()
    {
        List<Field> f = new ArrayList<>();
        f.add(new Field("[net:boot]", "host", "Server host",
                "a NAME, not an address - e.g. matthewllm"));
        f.add(new Field("[net:boot]", "port", "Server port", "43594"));
        f.add(new Field("[net:boot]", "ws_host", "Cache/CRC host",
                "usually the same machine"));
        f.add(new Field("[net:boot]", "ws_port", "Cache/CRC port", "80"));
        f.add(new Field("[cache:boot]", "dir", "Cache directory",
                "relative to this manifest"));
        /*
         * The IO server answers GET /boot/<path> for anything not on this
         * device -- the plugin manifest, plugin scripts, shipped plugin assets.
         * It is a separate host and port from the game server because it often
         * is a separate machine, and on a phone it is the one that most often
         * needs correcting: an empty host means "find everything locally",
         * which on a device with only a cache pushed to it is rarely true.
         */
        f.add(new Field("[io:boot]", "host", "IO server host",
                "blank = local files only"));
        f.add(new Field("[io:boot]", "port", "IO server port", "8081"));
        return f;
    }

    /** Fill in each field's current value from `manifest`. */
    static void load(File manifest, List<Field> fields)
    {
        try( BufferedReader r = new BufferedReader(new FileReader(manifest)) )
        {
            String line;
            String section = "";

            while( (line = r.readLine()) != null )
            {
                String t = line.trim();
                if( t.isEmpty() || t.startsWith(";") || t.startsWith("#") )
                    continue;
                if( t.startsWith("[") )
                {
                    section = t.toLowerCase();
                    continue;
                }
                int eq = t.indexOf('=');
                if( eq <= 0 )
                    continue;
                String key = t.substring(0, eq).trim();
                String val = stripComment(t.substring(eq + 1).trim());

                for( Field f : fields )
                {
                    if( f.section.equals(section) && f.key.equalsIgnoreCase(key) )
                    {
                        f.value = val;
                        f.present = true;
                    }
                }
            }
        }
        catch( Exception ignored )
        {
            /* An unreadable manifest shows empty fields rather than refusing to
             * open. The boot menu already reports the file as unreadable; this
             * screen should still let it be looked at. */
        }
    }

    /**
     * Write the edited values back, touching only the lines that hold them.
     *
     * @return null on success, or a message describing why it could not save.
     */
    static String save(File manifest, List<Field> fields)
    {
        List<String> out = new ArrayList<>();
        boolean[] written = new boolean[fields.size()];

        try( BufferedReader r = new BufferedReader(new FileReader(manifest)) )
        {
            String line;
            String section = "";

            while( (line = r.readLine()) != null )
            {
                String t = line.trim();

                if( t.startsWith("[") )
                {
                    section = t.toLowerCase();
                    out.add(line);
                    continue;
                }
                /* Comments and blanks pass through verbatim -- including a
                 * commented-out key, which must NOT be treated as the live one. */
                if( t.isEmpty() || t.startsWith(";") || t.startsWith("#") )
                {
                    out.add(line);
                    continue;
                }

                int eq = t.indexOf('=');
                if( eq > 0 )
                {
                    String key = t.substring(0, eq).trim();
                    int idx = indexOf(fields, section, key);
                    if( idx >= 0 )
                    {
                        Field f = fields.get(idx);
                        out.add(f.key + "=" + f.value);
                        written[idx] = true;
                        continue;
                    }
                }
                out.add(line);
            }
        }
        catch( Exception e )
        {
            return "could not read: " + e.getMessage();
        }

        /*
         * A field the file did not have, which the user has now filled in, is
         * appended under its section header. Appended rather than inserted at
         * the top of the section because insertion would have to guess where a
         * reader expects it; INI does not care, and a reader can see it at the
         * end of the block it belongs to.
         */
        for( int i = 0; i < fields.size(); i++ )
        {
            Field f = fields.get(i);
            if( written[i] || f.value.isEmpty() )
                continue;
            int at = lastLineOfSection(out, f.section);
            if( at < 0 )
                continue; /* no such section; adding one is beyond this editor */
            out.add(at + 1, f.key + "=" + f.value);
        }

        /*
         * Written through a temporary file and then renamed, so an interrupted
         * save cannot leave a half-written manifest behind. A truncated boot
         * file is worse than an unedited one: it fails at load with a parse
         * error that names a line rather than the interrupted write.
         */
        return writeAtomically(manifest, out);
    }

    /**
     * Write through a temporary file and rename.
     *
     * An interrupted save must not leave a half-written manifest: a truncated
     * boot file fails at load with a parse error naming a line, which says
     * nothing about the interrupted write that caused it.
     */
    private static String writeAtomically(File manifest, List<String> lines)
    {
        File tmp = new File(manifest.getAbsolutePath() + ".tmp");
        try( FileWriter w = new FileWriter(tmp) )
        {
            for( String l : lines )
            {
                w.write(l);
                w.write("\n");
            }
        }
        catch( Exception e )
        {
            return "could not write: " + e.getMessage();
        }
        if( !tmp.renameTo(manifest) )
        {
            tmp.delete();
            return "could not replace " + manifest.getName();
        }
        return null;
    }

    /**
     * Which renderer `[client:args]` currently selects: an index into
     * {@link #RENDERER_ARGS}, or 0 when it says nothing.
     */
    static int loadRenderer(File manifest)
    {
        try( BufferedReader r = new BufferedReader(new FileReader(manifest)) )
        {
            String line;
            String section = "";
            while( (line = r.readLine()) != null )
            {
                String t = line.trim();
                if( t.isEmpty() || t.startsWith(";") || t.startsWith("#") )
                    continue;
                if( t.startsWith("[") )
                {
                    section = t.toLowerCase();
                    continue;
                }
                if( !section.equals("[client:args]") )
                    continue;
                int eq = t.indexOf('=');
                if( eq <= 0 || !t.substring(0, eq).trim().equalsIgnoreCase("arg") )
                    continue;
                String v = t.substring(eq + 1).trim();
                for( int i = 1; i < RENDERER_ARGS.length; i++ )
                    if( RENDERER_ARGS[i].equals(v) )
                        return i;
            }
        }
        catch( Exception ignored )
        {
        }
        return 0;
    }

    /**
     * Rewrite `[client:args]` so it selects exactly `choice`.
     *
     * Every renderer arg already present is removed first -- two of them would
     * not be an error the client reports, it would simply take whichever the
     * argument parser saw last, which is a silent way to run the renderer you
     * did not pick. Other args in the section are left alone.
     *
     * @return null on success, otherwise the reason.
     */
    static String saveRenderer(File manifest, int choice)
    {
        List<String> out = new ArrayList<>();
        int sectionEnd = -1;

        try( BufferedReader r = new BufferedReader(new FileReader(manifest)) )
        {
            String line;
            String section = "";
            while( (line = r.readLine()) != null )
            {
                String t = line.trim();
                if( t.startsWith("[") )
                {
                    if( section.equals("[client:args]") && sectionEnd < 0 )
                        sectionEnd = out.size();
                    section = t.toLowerCase();
                    out.add(line);
                    continue;
                }
                if( section.equals("[client:args]") && !t.isEmpty()
                        && !t.startsWith(";") && !t.startsWith("#") )
                {
                    int eq = t.indexOf('=');
                    if( eq > 0 && t.substring(0, eq).trim().equalsIgnoreCase("arg") )
                    {
                        String v = t.substring(eq + 1).trim();
                        boolean isRenderer = false;
                        for( int i = 1; i < RENDERER_ARGS.length; i++ )
                            if( RENDERER_ARGS[i].equals(v) )
                                isRenderer = true;
                        if( isRenderer )
                            continue; /* drop it; the choice is re-added below */
                    }
                }
                out.add(line);
            }
            if( section.equals("[client:args]") && sectionEnd < 0 )
                sectionEnd = out.size();
        }
        catch( Exception e )
        {
            return "could not read: " + e.getMessage();
        }

        if( choice > 0 )
        {
            if( sectionEnd < 0 )
            {
                /* No [client:args] at all: start one at the end of the file. */
                out.add("");
                out.add("[client:args]");
                out.add("arg=" + RENDERER_ARGS[choice]);
            }
            else
            {
                out.add(sectionEnd, "arg=" + RENDERER_ARGS[choice]);
            }
        }

        return writeAtomically(manifest, out);
    }

    private static int indexOf(List<Field> fields, String section, String key)
    {
        for( int i = 0; i < fields.size(); i++ )
        {
            Field f = fields.get(i);
            if( f.section.equals(section) && f.key.equalsIgnoreCase(key) )
                return i;
        }
        return -1;
    }

    /** Index of the last non-blank line inside `section`, or -1. */
    private static int lastLineOfSection(List<String> lines, String section)
    {
        int start = -1;
        for( int i = 0; i < lines.size(); i++ )
        {
            String t = lines.get(i).trim();
            if( !t.startsWith("[") )
                continue;
            if( t.toLowerCase().equals(section) )
                start = i;
            else if( start >= 0 )
                return i - 1; /* the line before the NEXT section header */
        }
        return start >= 0 ? lines.size() - 1 : -1;
    }

    private static String stripComment(String value)
    {
        int cut = value.length();
        for( int i = 0; i < value.length(); i++ )
        {
            char c = value.charAt(i);
            if( c == ';' || c == '#' )
            {
                cut = i;
                break;
            }
        }
        return value.substring(0, cut).trim();
    }

    private ProfileEditor()
    {
    }
}
