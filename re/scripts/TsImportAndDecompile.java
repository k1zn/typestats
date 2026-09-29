// Ghidra headless script for TypeStats.exe (C++Builder 6 / VCL).
// 1. Registers a Borland register calling convention (EAX, EDX, ECX, then stack).
// 2. Applies symbols from re/vcl_symbols.json (VMT/RTTI-derived names).
// 3. Creates structs for form classes with published fields.
// 4. Decompiles all functions in the application code range into re/decomp/*.c.
// Args: <repo root>
//@category TypeStats

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.database.SpecExtension;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.TaskMonitor;
import com.google.gson.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class TsImportAndDecompile extends GhidraScript {

    static final String BORLAND =
        "<prototype name=\"__borland\" extrapop=\"unknown\" stackshift=\"4\">\n" +
        "  <input>\n" +
        "    <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EAX\"/></pentry>\n" +
        "    <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EDX\"/></pentry>\n" +
        "    <pentry minsize=\"1\" maxsize=\"4\"><register name=\"ECX\"/></pentry>\n" +
        "    <pentry minsize=\"1\" maxsize=\"500\" align=\"4\"><addr offset=\"4\" space=\"stack\"/></pentry>\n" +
        "  </input>\n" +
        "  <output>\n" +
        "    <pentry minsize=\"4\" maxsize=\"10\" metatype=\"float\"><register name=\"ST0\"/></pentry>\n" +
        "    <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EAX\"/></pentry>\n" +
        "    <pentry minsize=\"5\" maxsize=\"8\"><addr space=\"join\" piece1=\"EDX\" piece2=\"EAX\"/></pentry>\n" +
        "  </output>\n" +
        "  <unaffected><register name=\"ESP\"/><register name=\"EBP\"/><register name=\"ESI\"/><register name=\"EDI\"/><register name=\"EBX\"/></unaffected>\n" +
        "  <killedbycall><register name=\"EAX\"/><register name=\"ECX\"/><register name=\"EDX\"/><register name=\"ST0\"/></killedbycall>\n" +
        "</prototype>\n";

    @Override
    public void run() throws Exception {
        String root = getScriptArgs()[0];
        String mode = getScriptArgs().length > 1 ? getScriptArgs()[1] : "all";
        Listing listing = currentProgram.getListing();
        SymbolTable st = currentProgram.getSymbolTable();

        if (!mode.equals("decompile")) {
            try {
                SpecExtension ext = new SpecExtension(currentProgram);
                ext.addReplaceCompilerSpecExtension(BORLAND, monitor);
                println("Added __borland prototype");
            } catch (Exception e) { println("spec ext: " + e); }

            JsonObject j = JsonParser.parseString(Files.readString(Path.of(root, "re", "vcl_symbols.json"), StandardCharsets.UTF_8)).getAsJsonObject();
            // functions
            int n = 0;
            for (Map.Entry<String, JsonElement> e : j.getAsJsonObject("functions").entrySet()) {
                Address a = toAddr(Long.decode(e.getKey()));
                String name = e.getValue().getAsString();
                disassemble(a);
                Function f = getFunctionAt(a);
                if (f == null) f = createFunction(a, null);
                if (f == null) continue;
                String[] parts = name.split("::");
                Namespace ns = st.getNamespace(parts[0], null);
                if (ns == null) ns = st.createClass(null, parts[0], SourceType.IMPORTED);
                f.setName(parts[1], SourceType.IMPORTED);
                f.setParentNamespace(ns);
                f.setCallingConvention("__borland");
                n++;
            }
            println("Named functions: " + n);
            // VMT labels + form structs
            DataTypeManager dtm = currentProgram.getDataTypeManager();
            CategoryPath cat = new CategoryPath("/VCL");
            JsonObject classes = j.getAsJsonObject("classes");
            for (Map.Entry<String, JsonElement> e : classes.entrySet()) {
                JsonObject c = e.getValue().getAsJsonObject();
                createLabel(toAddr(c.get("vmt").getAsLong()), "vmt_" + e.getKey(), true);
            }
            for (Map.Entry<String, JsonElement> e : classes.entrySet()) {
                String cls = e.getKey();
                JsonObject c = e.getValue().getAsJsonObject();
                JsonArray fields = c.getAsJsonArray("fields");
                if (fields.size() == 0 && !cls.startsWith("TForm") && !cls.equals("Tkbd")) continue;
                int size = c.get("size").getAsInt();
                StructureDataType s = new StructureDataType(cat, cls, size, dtm);
                s.replaceAtOffset(0, new PointerDataType(VoidDataType.dataType), 4, "vmt", null);
                for (JsonElement fe : fields) {
                    JsonArray fa = fe.getAsJsonArray();
                    int off = fa.get(0).getAsInt();
                    String fname = fa.get(1).getAsString();
                    if (off + 4 <= size) s.replaceAtOffset(off, new PointerDataType(VoidDataType.dataType), 4, fname, null);
                }
                DataType dt = dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
                // retype `this` of this class's methods
                Namespace ns = st.getNamespace(cls, null);
                if (ns == null) continue;
                for (Symbol sym : st.getSymbols(ns)) {
                    Function f = getFunctionAt(sym.getAddress());
                    if (f == null) continue;
                    try {
                        Parameter p0 = new ParameterImpl("this", new PointerDataType(dt), currentProgram);
                        f.updateFunction("__borland", null, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED, p0);
                    } catch (Exception ex) { }
                }
            }
        }

        if (mode.equals("names")) return;
        // decompile app range
        long lo = Long.decode(System.getProperty("ts.lo", "0x401000"));
        long hi = Long.decode(System.getProperty("ts.hi", "0x456000"));
        DecompInterface di = new DecompInterface();
        DecompileOptions opt = new DecompileOptions();
        di.setOptions(opt);
        di.toggleCCode(true);
        di.openProgram(currentProgram);
        Path outDir = Path.of(root, "re", "decomp");
        Files.createDirectories(outDir);
        Map<String, StringBuilder> files = new TreeMap<>();
        FunctionIterator it = listing.getFunctions(toAddr(lo), true);
        int cnt = 0;
        while (it.hasNext() && !monitor.isCancelled()) {
            Function f = it.next();
            if (f.getEntryPoint().getOffset() >= hi) break;
            String ns = f.getParentNamespace().isGlobal() ? "_global" : f.getParentNamespace().getName();
            // group unnamed functions by the nearest preceding named class
            String key = String.format("%06x", f.getEntryPoint().getOffset() & 0xff0000);
            StringBuilder sb = files.computeIfAbsent(key, k -> new StringBuilder());
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            sb.append("\n// ===== ").append(f.getEntryPoint()).append(" ").append(ns).append("::").append(f.getName()).append("\n");
            if (r != null && r.decompileCompleted()) sb.append(r.getDecompiledFunction().getC());
            else sb.append("// decompile failed: ").append(r == null ? "null" : r.getErrorMessage()).append("\n");
            cnt++;
        }
        for (Map.Entry<String, StringBuilder> e : files.entrySet())
            Files.writeString(outDir.resolve("range_" + e.getKey() + ".c"), e.getValue().toString(), StandardCharsets.UTF_8);
        println("Decompiled " + cnt + " functions");
    }
}
