# Normalize Nothing kernel's OC option in the unpacked boot header.
# Remove every old occurrence so OC -> no-OC -> OC installs are repeatable.
nothing_patch_cmdline() {
  local cmdfile header tmp;
  case "$1" in
    0|1) ;;
    *) abort "Invalid Nothing kernel OC mode."; return 1;;
  esac;

  header=0;
  if [ -f "$SPLITIMG/cmdline.txt" ]; then
    cmdfile=$SPLITIMG/cmdline.txt;
  elif [ -f "$SPLITIMG/header" ]; then
    cmdfile=$SPLITIMG/header;
    header=1;
  else
    abort "Boot command line could not be found.";
    return 1;
  fi;
  tmp=$cmdfile.nothing;

  if ! awk -v mode="$1" -v header="$header" '
    function normalize(line, args, n, i, result) {
      n = split(line, args, /[[:space:]]+/);
      result = "";
      for (i = 1; i <= n; i++) {
        if (args[i] != "" && args[i] !~ /^nothing[.]oc(=|$)/)
          result = result args[i] " ";
      }
      return result "nothing.oc=" mode;
    }
    !header { print normalize($0); found++; next; }
    /^cmdline=/ { print "cmdline=" normalize(substr($0, 9)); found++; next; }
    { print; }
    END {
      if (!header && found == 0) { print "nothing.oc=" mode; found++; }
      if (found != 1) exit 1;
    }
  ' "$cmdfile" > "$tmp"; then
    rm -f "$tmp";
    abort "Could not update Nothing kernel OC option.";
    return 1;
  fi;
  mv -f "$tmp" "$cmdfile" || {
    abort "Could not save Nothing kernel OC option.";
    return 1;
  };
}
