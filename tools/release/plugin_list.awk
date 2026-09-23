# Edit MPC.settings' plugin list (BusyBox awk). Shipped in every release zip (tools/release.py).
#   awk -v mode=add    -v file=/sdcard/vst/x.so -v entryfile=plugin.xml -f plugin_list.awk MPC.settings
#   awk -v mode=remove -v file=/sdcard/vst/x.so -f plugin_list.awk MPC.settings
# Drops every <PLUGIN .../> (single- or multi-line) whose file= matches, then in add mode inserts the entry
# into <VALUE name="pluginList-arm"><KNOWNPLUGINS>, creating the value before </PROPERTIES> if it's missing.
function flush() {
    if (index(buf, "file=\"" file "\"") == 0) print buf
    buf = ""
}
BEGIN {
    if (mode == "add") { while ((getline l < entryfile) > 0) entry = entry l; close(entryfile) }
    done = 0; inlist = 0; buf = ""
}
buf != "" { buf = buf "\n" $0; if ($0 ~ /\/>/) flush(); next }
/<PLUGIN( |$)/ { buf = $0; if ($0 ~ /\/>/) flush(); next }
/<VALUE name="pluginList-arm">/ { inlist = 1; print; next }
inlist && /<KNOWNPLUGINS\/>/ {
    if (mode == "add") {
        ind = $0; sub(/<.*/, "", ind)
        print ind "<KNOWNPLUGINS>"; print ind "  " entry; print ind "</KNOWNPLUGINS>"; done = 1
    } else print
    inlist = 0; next
}
inlist && /<\/KNOWNPLUGINS>/ {
    if (mode == "add" && !done) { ind = $0; sub(/<.*/, "", ind); print ind "  " entry; done = 1 }
    inlist = 0; print; next
}
/<\/PROPERTIES>/ {
    if (mode == "add" && !done) {
        print "  <VALUE name=\"pluginList-arm\">"; print "    <KNOWNPLUGINS>"; print "      " entry
        print "    </KNOWNPLUGINS>"; print "  </VALUE>"; done = 1
    }
    print; next
}
{ print }
