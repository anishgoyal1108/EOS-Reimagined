#!/usr/bin/env python3
"""Generate honest compatibility shells for every EOS export we do not implement.

A game that statically imports -- or, for a Unity/Mono title, P/Invokes -- an EOS symbol we do not
export gets a hard failure: the loader refuses the process, or the CLR throws
EntryPointNotFoundException on first call. Neither is something the emulator can trace or explain,
because it never gets control. So every function the SDK declares is exported, and the ones we have
not built answer honestly:

  * an EOS_EResult function returns EOS_NotImplemented;
  * an asynchronous function still *fires its callback* -- with EOS_NotImplemented -- on the next
    tick, so a game that awaits a completion is never left hanging;
  * an AddNotify* registers a real notification that simply never fires, so the game gets a valid id
    it can pair a Remove with;
  * a getter reports nothing (zero / false / null / empty), never a fabricated value;
  * a *_Release is a no-op, because there is nothing we allocated to free.

Nothing here pretends to work. The point is that the game runs far enough to tell us what it needs.
"""

import re
import sys
from pathlib import Path

HEADERS = Path("include/eos")
FLAT = Path("src/flat")
OUT = Path("src/flat/eos_stubs_flat.cpp")

# EOS_DECLARE_FUNC(ret) name(args);  -- possibly wrapped over several lines.
FUNC = re.compile(r"EOS_DECLARE_FUNC\(\s*(.+?)\s*\)\s+(EOS_[A-Za-z0-9_]+)\s*\((.*?)\)\s*;", re.S)
# EOS_DECLARE_CALLBACK(Name, const Info* Data);
CB = re.compile(r"EOS_DECLARE_CALLBACK\(\s*(EOS_[A-Za-z0-9_]+)\s*,\s*const\s+(EOS_[A-Za-z0-9_]+)\s*\*", re.S)
CB_RET = re.compile(r"EOS_DECLARE_CALLBACK_RETVALUE\(\s*[^,]+,\s*(EOS_[A-Za-z0-9_]+)\s*,", re.S)


def implemented():
    """The EOS_* functions the hand-written flat layer defines.

    Read from the sources, never from the built library: the built library already contains the last
    generation's stubs, so measuring against it would report nothing left to do and erase this file.
    The generator has to be idempotent -- running it twice must produce the same output.
    """
    found = set()
    for src in sorted(FLAT.glob("*.cpp")):
        if src.name == OUT.name:
            continue  # the generated file itself is not a hand-written implementation
        text = src.read_text(errors="replace")
        found |= set(re.findall(r"EOS_DECLARE_FUNC\([^)]*\)\s+(EOS_[A-Za-z0-9_]+)\s*\(", text))
        # The interface getters are emitted by a macro, not written out one by one, so they carry no
        # literal EOS_DECLARE_FUNC. Missing them would re-stub a function we already define.
        found |= set(re.findall(r"EOSR_INTERFACE_GETTER\(\s*(EOS_[A-Za-z0-9_]+)", text))
    return found


def parse_headers():
    # Deprecated exports are declared in .inl files that a _types.h pulls in, so we must read those
    # too -- a game bound to an older SDK still P/Invokes them. We attribute each declaration to the
    # header that includes the .inl, because that is what a stub must include to see the real types.
    inl_parent = {}
    for h in HEADERS.glob("*.h"):
        for inl in re.findall(r'#include\s+"([^"]+\.inl)"', h.read_text(errors="replace")):
            inl_parent[inl] = h.name

    funcs, delegate_info, ret_delegates = {}, {}, set()
    sources = sorted(HEADERS.glob("*.h")) + sorted(HEADERS.glob("*.inl"))
    for h in sources:
        if h.name == "eos_base.h":
            continue  # it *defines* the EOS_DECLARE_* macros; it declares no functions
        owner = inl_parent.get(h.name, h.name)
        text = h.read_text(errors="replace")
        for name, info in CB.findall(text):
            delegate_info[name] = info
        for name in CB_RET.findall(text):
            ret_delegates.add(name)
        for ret, name, args in FUNC.findall(text):
            ret = " ".join(ret.split())
            args = " ".join(args.split())
            if name not in funcs:
                funcs[name] = (ret, args, owner)
    return funcs, delegate_info, ret_delegates


def split_params(args):
    if not args or args.strip() == "void":
        return []
    out, depth, cur = [], 0, ""
    for c in args:
        if c == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        if c in "(<":
            depth += 1
        elif c in ")>":
            depth -= 1
        cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


def param_name(p):
    # "const EOS_Ecom_QueryOwnershipOptions* Options" -> Options ; "void* ClientData" -> ClientData
    m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*(\[\s*\])?$", p)
    return m.group(1) if m else None


def param_type(p):
    n = param_name(p)
    return p[: p.rfind(n)].strip() if n else p.strip()


def output_initializers(params):
    lines = []
    for p in params:
        name = param_name(p)
        type_name = param_type(p)
        if not name or not name.startswith("Out") or "*" not in type_name:
            continue
        if type_name.lstrip().startswith("const ") or type_name.replace(" ", "") in ("char*", "void*"):
            continue
        pointee = type_name.rsplit("*", 1)[0].strip()
        lines.append("    if (%s != NULL) { *%s = static_cast<%s>(0); }\n" %
                     (name, name, pointee))
    return "".join(lines)


def notification_event(name):
    match = re.match(r"EOS_(.+)_AddNotify(.+)$", name)
    if not match:
        return name[4:] if name.startswith("EOS_") else name
    family = match.group(1).replace("_", "")
    event = match.group(2)
    overlap = 0
    for size in range(1, min(len(family), len(event)) + 1):
        if family[-size:] == event[:size]:
            overlap = size
    return family + event[overlap:]


def stub_body(name, ret, params, delegate_info):
    """The honest answer for one unimplemented export."""
    unused = "".join("    (void)%s;\n" % param_name(p) for p in params if param_name(p))

    # Find a completion/notification delegate parameter, and the CallbackInfo it expects.
    delegate, info = None, None
    for p in params:
        t = param_type(p).replace("const", "").strip()
        if t in delegate_info:
            delegate, info = param_name(p), delegate_info[t]
            break

    initialize_outputs = output_initializers(params)
    complete = ""
    if delegate and info:
        client_data = "ClientData" if any(param_name(p) == "ClientData" for p in params) else "NULL"
        complete = (
            "    eosr::stub_complete(%s,\n"
            "        reinterpret_cast<eosr::completion_delegate>(%s), sizeof(%s));\n"
            % (client_data, delegate, info)
        )

    if ret == "EOS_NotificationId":
        if delegate and info:
            # A real id backed by a notification that never fires: the game can pair a Remove with it.
            return (
                unused
                + "    return eosr::stub_add_notification(ClientData,\n"
                "        reinterpret_cast<eosr::completion_delegate>(%s), sizeof(%s), \"%s\");\n"
                % (delegate, info, notification_event(name))
            )
        return unused + "    return EOS_INVALID_NOTIFICATIONID;\n"

    if ret == "void":
        if delegate and info:
            return unused + initialize_outputs + complete
        if "RemoveNotify" in name and params:
            last = param_name(params[-1])
            return (
                "".join("    (void)%s;\n" % param_name(p) for p in params[:-1] if param_name(p))
                + "    eosr::stub_remove_notification(%s);\n" % last
            )
        return unused + initialize_outputs  # a _Release, or a setter with nothing behind it
    if ret == "EOS_EResult":
        return unused + initialize_outputs + complete + "    return EOS_EResult::EOS_NotImplemented;\n"
    if ret == "EOS_Bool":
        return unused + initialize_outputs + complete + "    return EOS_FALSE;\n"
    if ret == "const char*":
        return unused + initialize_outputs + complete + '    return "";\n'
    if ret in ("uint32_t", "int32_t", "uint64_t", "int64_t", "double", "float"):
        return unused + initialize_outputs + complete + "    return 0;\n"
    if ret.endswith("*") or ret.startswith("EOS_H"):
        return unused + initialize_outputs + complete + "    return NULL;\n"
    # An opaque id handle (EOS_ProductUserId / EOS_EpicAccountId / ...) is a pointer typedef.
    return unused + initialize_outputs + complete + "    return NULL;\n"


def main():
    have = implemented()
    funcs, delegate_info, _ = parse_headers()

    todo = {n: v for n, v in funcs.items() if n not in have}
    if "--survey" in sys.argv:
        from collections import Counter
        print("declared in headers : %d" % len(funcs))
        print("already exported    : %d" % len([n for n in funcs if n in have]))
        print("to stub             : %d" % len(todo))
        print("\nreturn types to handle:")
        for ret, n in Counter(v[0] for v in todo.values()).most_common():
            print("  %-24s %d" % (ret, n))
        return 0

    by_header = {}
    for name, (ret, args, header) in sorted(todo.items()):
        by_header.setdefault(header, []).append((name, ret, args))

    lines = [
        "// GENERATED by tools/gen_stubs.py -- do not edit by hand.",
        "//",
        "// Honest compatibility shells for every EOS export we have not built. A game that imports or",
        "// P/Invokes a symbol we do not export cannot even start (the loader refuses it) or throws on",
        "// first call (EntryPointNotFoundException) -- and in neither case does the emulator get control,",
        "// so it cannot trace or explain the failure. Exporting the whole surface turns a hard failure",
        "// into an honest EOS_NotImplemented that a game can see, and that our trace can record.",
        "//",
        "// Nothing here pretends to work. Asynchronous stubs still fire their callback, so a game",
        "// awaiting a completion is never left hanging; AddNotify* returns a real id backed by a",
        "// notification that never fires; getters report nothing rather than a made-up value.",
        "",
    ]
    includes = sorted({h for h in by_header})
    for h in includes:
        lines.append('#include "%s"' % h)
    lines += ["", '#include "core/stub_completion.h"', ""]

    for header in includes:
        lines.append("// --- %s ---" % header)
        lines.append("")
        for name, ret, args in by_header[header]:
            params = split_params(args)
            sig = ", ".join(params) if params else "void"
            lines.append("EOS_DECLARE_FUNC(%s) %s(%s) {" % (ret, name, sig))
            lines.append(stub_body(name, ret, params, delegate_info).rstrip("\n"))
            lines.append("}")
            lines.append("")

    OUT.write_text("\n".join(lines).rstrip("\n") + "\n")  # exactly one trailing newline
    print("wrote %s: %d stubs across %d headers" % (OUT, len(todo), len(by_header)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
