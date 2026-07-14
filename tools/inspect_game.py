#!/usr/bin/env python3
"""inspect_game -- the EOS import census.

Reads a game's binaries and answers one question: if our library replaced the real EOS SDK, would
the game still load, and what would it be missing? It inspects only; it never stages or writes into
a game directory. The report goes to inspection.json.

The census walks every native module (PE and ELF) and every managed assembly under the target,
recording:
  * static native imports        -- a missing one of these is loader-fatal: the game never starts.
  * delay-loaded native imports  -- a missing one fails at first call, not at load.
  * .NET P/Invoke (ImplMap)      -- Unity/Mono games declare their EOS entry points here.
  * dynamic resolution           -- GetProcAddress/dlsym plus EOS_* and EOSSDK strings in the image.

Pure stdlib: this ships nothing and depends on nothing, like the rest of the project.
"""

import argparse
import hashlib
import json
import re
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path

# ---------------------------------------------------------------------------------------------
# Small binary readers. Both formats are read straight out of the file image; we never load or
# execute anything we are inspecting.
# ---------------------------------------------------------------------------------------------

PE_MACHINE = {0x8664: "x86-64", 0x014C: "x86", 0xAA64: "arm64"}
ELF_MACHINE = {0x3E: "x86-64", 0x03: "x86", 0xB7: "arm64"}

EOS_SYMBOL = re.compile(rb"\bEOS_[A-Za-z0-9_]{2,96}")
EOS_MODULE = re.compile(rb"[A-Za-z0-9_\-.]*EOSSDK[A-Za-z0-9_\-.]*\.(?:dll|so)", re.IGNORECASE)


class Pe:
    """Just enough PE to read the import, delay-import, export and CLI directories."""

    def __init__(self, data):
        self.data = data
        self.ok = False
        if len(data) < 0x40 or data[:2] != b"MZ":
            return
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if e_lfanew + 24 > len(data) or data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
            return
        coff = e_lfanew + 4
        machine, nsections, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, coff)
        self.arch = PE_MACHINE.get(machine, "0x%04x" % machine)
        opt = coff + 20
        magic = struct.unpack_from("<H", data, opt)[0]
        self.plus = magic == 0x20B  # PE32+ (64-bit)
        # The data-directory array sits after the optional header's fixed part, which differs
        # between PE32 and PE32+ only in the fields we skip over.
        dd = opt + (112 if self.plus else 96)
        ndirs = struct.unpack_from("<I", data, opt + (108 if self.plus else 92))[0]
        self.dirs = []
        for i in range(min(ndirs, 16)):
            rva, size = struct.unpack_from("<II", data, dd + i * 8)
            self.dirs.append((rva, size))
        self.sections = []
        sec = opt + opt_size
        for i in range(nsections):
            base = sec + i * 40
            if base + 40 > len(data):
                break
            name, vsize, vaddr, rawsize, rawptr = struct.unpack_from("<8sIIII", data, base)
            self.sections.append((vaddr, max(vsize, rawsize), rawptr, rawsize))
        self.ok = True

    def off(self, rva):
        """File offset for a virtual address, or None when it lands outside every section."""
        for vaddr, vsize, rawptr, rawsize in self.sections:
            if vaddr <= rva < vaddr + vsize:
                delta = rva - vaddr
                if delta < rawsize:
                    return rawptr + delta
                return None
        return None

    def cstr(self, off):
        if off is None or off >= len(self.data):
            return ""
        end = self.data.find(b"\0", off)
        return self.data[off : end if end != -1 else len(self.data)].decode("ascii", "replace")

    def _thunk_names(self, thunk_rva):
        """Walk one import name table, yielding the imported symbol names (ordinals are skipped)."""
        names = []
        width = 8 if self.plus else 4
        ordinal_flag = (1 << 63) if self.plus else (1 << 31)
        off = self.off(thunk_rva)
        if off is None:
            return names
        while True:
            if off + width > len(self.data):
                break
            raw = int.from_bytes(self.data[off : off + width], "little")
            if raw == 0:
                break
            if not (raw & ordinal_flag):
                # A hint/name entry: 2-byte hint, then the ASCII name.
                name_off = self.off(raw & 0x7FFFFFFF)
                if name_off is not None:
                    names.append(self.cstr(name_off + 2))
            off += width
        return names

    def imports(self, directory=1):
        """{dll: [symbols]} for the import (1) or delay-import (13) directory."""
        out = {}
        if directory >= len(self.dirs):
            return out
        rva, _ = self.dirs[directory]
        if not rva:
            return out
        base = self.off(rva)
        if base is None:
            return out
        # An import descriptor is 20 bytes; a delay descriptor is 32 and its name/thunk fields sit
        # at different offsets. Both are terminated by an all-zero record.
        stride = 32 if directory == 13 else 20
        i = 0
        while True:
            rec = base + i * stride
            if rec + stride > len(self.data):
                break
            fields = struct.unpack_from("<%dI" % (stride // 4), self.data, rec)
            if not any(fields):
                break
            if directory == 13:
                name_rva, thunk_rva = fields[2], fields[4]  # DllNameRVA, ImportNameTableRVA
            else:
                thunk_rva, name_rva = fields[0] or fields[4], fields[3]  # OFT else FT, Name
            dll = self.cstr(self.off(name_rva))
            if dll:
                out.setdefault(dll, []).extend(self._thunk_names(thunk_rva))
            i += 1
        return out

    def exports(self):
        out = []
        if not self.dirs or not self.dirs[0][0]:
            return out
        base = self.off(self.dirs[0][0])
        if base is None:
            return out
        count, names_rva = struct.unpack_from("<I", self.data, base + 24)[0], struct.unpack_from(
            "<I", self.data, base + 32
        )[0]
        table = self.off(names_rva)
        if table is None:
            return out
        for i in range(min(count, 65536)):
            if table + i * 4 + 4 > len(self.data):
                break
            rva = struct.unpack_from("<I", self.data, table + i * 4)[0]
            name = self.cstr(self.off(rva))
            if name:
                out.append(name)
        return out

    def dotnet(self):
        """The CLI header's metadata blob, or None for a purely native image."""
        if len(self.dirs) < 15 or not self.dirs[14][0]:
            return None
        cli = self.off(self.dirs[14][0])
        if cli is None or cli + 16 > len(self.data):
            return None
        meta_rva, meta_size = struct.unpack_from("<II", self.data, cli + 8)
        off = self.off(meta_rva)
        if off is None:
            return None
        return self.data[off : off + meta_size]


def dotnet_pinvoke(meta):
    """The P/Invoke entry-point names a managed assembly declares (the ImplMap table).

    We do not decode the whole table stream -- the row widths depend on every other table's size.
    The #Strings heap holds every name the metadata refers to, and an ImplMap row's ImportName is
    one of them, so we read the heap and let the caller intersect it with the EOS surface. That
    over-approximates the P/Invoke set by including other metadata names, which is why the caller
    only ever keeps names that look like EOS entry points.
    """
    if not meta or meta[:4] != b"BSJB":
        return []
    ver_len = struct.unpack_from("<I", meta, 12)[0]
    off = 16 + ver_len + 4  # skip version string, then flags
    streams = struct.unpack_from("<H", meta, off - 2)[0]
    off = 16 + ver_len + 4
    heap = None
    for _ in range(streams):
        if off + 8 > len(meta):
            break
        s_off, s_size = struct.unpack_from("<II", meta, off)
        end = meta.find(b"\0", off + 8)
        name = meta[off + 8 : end].decode("ascii", "replace")
        off = end + 1
        off = (off + 3) & ~3  # names are padded to a 4-byte boundary
        if name == "#Strings":
            heap = meta[s_off : s_off + s_size]
    if heap is None:
        return []
    return [s.decode("ascii", "replace") for s in heap.split(b"\0") if s]


class Elf:
    def __init__(self, data):
        self.data = data
        self.ok = False
        self.undefined = []
        self.needed = []
        if len(data) < 64 or data[:4] != b"\x7fELF":
            return
        self.bits = 64 if data[4] == 2 else 32
        self.arch = ELF_MACHINE.get(struct.unpack_from("<H", data, 18)[0], "?")
        if self.bits != 64:
            self.ok = True  # 32-bit ELF: recognized, symbols not walked (no 32-bit EOS SDK exists)
            return
        e_shoff, = struct.unpack_from("<Q", data, 0x28)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 0x3A)
        sections = []
        for i in range(e_shnum):
            b = e_shoff + i * e_shentsize
            if b + 64 > len(data):
                break
            sh_name, sh_type, _, _, sh_off, sh_size, sh_link, _, _, sh_entsize = struct.unpack_from(
                "<IIQQQQIIQQ", data, b
            )
            sections.append((sh_name, sh_type, sh_off, sh_size, sh_link, sh_entsize))
        for sh_name, sh_type, sh_off, sh_size, sh_link, sh_entsize in sections:
            if sh_type == 11 and sh_entsize:  # SHT_DYNSYM
                strtab = sections[sh_link] if sh_link < len(sections) else None
                if not strtab:
                    continue
                str_off, str_size = strtab[2], strtab[3]
                for k in range(sh_size // sh_entsize):
                    e = sh_off + k * sh_entsize
                    if e + 24 > len(data):
                        break
                    st_name, _, _, st_shndx = struct.unpack_from("<IBBH", data, e)
                    if st_shndx != 0 or not st_name:  # SHN_UNDEF == an import
                        continue
                    p = str_off + st_name
                    end = data.find(b"\0", p)
                    self.undefined.append(data[p:end].decode("ascii", "replace"))
        self.ok = True


# ---------------------------------------------------------------------------------------------
# The census
# ---------------------------------------------------------------------------------------------

NATIVE_SUFFIX = {".exe", ".dll", ".so", ".node"}


def family_of(symbol):
    """EOS_Connect_Login -> Connect. The handle-free helpers land in Common."""
    parts = symbol.split("_")
    if len(parts) < 3:
        return "Common"
    return parts[1]


def looks_like_eos_module(path):
    return "eossdk" in path.name.lower()


def scan_module(path, our_exports):
    data = path.read_bytes()
    rec = {
        "path": str(path),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    imported_static, imported_delayed, pinvoke = [], [], []
    dyn_symbols = sorted({m.decode() for m in EOS_SYMBOL.findall(data)})
    dyn_modules = sorted({m.decode() for m in EOS_MODULE.findall(data)})

    if data[:2] == b"MZ":
        pe = Pe(data)
        if not pe.ok:
            return None
        rec["format"] = "pe"
        rec["arch"] = pe.arch
        rec["is_eos_artifact"] = looks_like_eos_module(path)
        exports = pe.exports()
        if rec["is_eos_artifact"]:
            rec["exports_eos"] = sorted(s for s in exports if s.startswith("EOS_"))
        static = pe.imports(1)
        delayed = pe.imports(13)
        rec["imported_modules"] = sorted(set(static) | set(delayed))
        for dll, syms in static.items():
            imported_static += [s for s in syms if s.startswith("EOS_")]
        for dll, syms in delayed.items():
            imported_delayed += [s for s in syms if s.startswith("EOS_")]
        rec["uses_runtime_linking"] = any(
            "GetProcAddress" in s for syms in static.values() for s in syms
        )
        meta = pe.dotnet()
        if meta is not None:
            rec["is_dotnet"] = True
            names = dotnet_pinvoke(meta)
            pinvoke = sorted({n for n in names if n.startswith("EOS_")})
        else:
            rec["is_dotnet"] = False
    elif data[:4] == b"\x7fELF":
        elf = Elf(data)
        if not elf.ok:
            return None
        rec["format"] = "elf"
        rec["arch"] = elf.arch
        rec["is_eos_artifact"] = looks_like_eos_module(path)
        rec["is_dotnet"] = False
        imported_static = [s for s in elf.undefined if s.startswith("EOS_")]
        rec["uses_runtime_linking"] = any(s == "dlsym" for s in elf.undefined)
    else:
        return None

    # A module that *is* an EOS SDK exports the surface; it does not import it.
    if rec.get("is_eos_artifact"):
        imported_static, imported_delayed, pinvoke = [], [], []
        dyn_symbols = []

    rec["imports_eos"] = {
        "static": sorted(set(imported_static)),
        "delayed": sorted(set(imported_delayed)),
        "pinvoke": sorted(set(pinvoke)),
    }
    rec["eos_symbol_strings"] = [] if rec.get("is_eos_artifact") else dyn_symbols
    rec["eos_module_strings"] = dyn_modules
    rec["touches_eos"] = bool(
        imported_static or imported_delayed or pinvoke or dyn_symbols or dyn_modules
    )
    return rec


def load_exports(path):
    """Our exported EOS_* surface, read from the built library itself."""
    data = Path(path).read_bytes()
    if data[:2] == b"MZ":
        pe = Pe(data)
        return sorted(s for s in pe.exports() if s.startswith("EOS_"))
    if data[:4] == b"\x7fELF":
        # Our own .so: the exported (defined) dynamic symbols, not the undefined ones.
        out = set()
        for m in EOS_SYMBOL.findall(data):
            out.add(m.decode())
        return sorted(out)
    return []


def main():
    ap = argparse.ArgumentParser(description="EOS import census for a game directory.")
    ap.add_argument("target", help="the game directory (or a single binary) to inspect")
    ap.add_argument("--our-library", required=True, help="our built .dll/.so, for the export surface")
    ap.add_argument("--reference-sdk", help="a real Epic EOSSDK binary, for the full EOS surface")
    ap.add_argument("--out", default="inspection.json")
    args = ap.parse_args()

    our_exports = set(load_exports(args.our_library))
    reference = {}
    if args.reference_sdk:
        ref_path = Path(args.reference_sdk)
        ref_data = ref_path.read_bytes()
        ref_pe = Pe(ref_data)
        ref_exports = sorted(s for s in ref_pe.exports() if s.startswith("EOS_")) if ref_pe.ok else []
        reference = {
            "path": str(ref_path),
            "sha256": hashlib.sha256(ref_data).hexdigest(),
            "exports": len(ref_exports),
            "symbols": ref_exports,
        }

    target = Path(args.target)
    files = [target] if target.is_file() else [
        p for p in sorted(target.rglob("*")) if p.is_file() and p.suffix.lower() in NATIVE_SUFFIX
    ]

    modules, scanned = [], 0
    for p in files:
        try:
            rec = scan_module(p, our_exports)
        except Exception as exc:  # a truncated or hostile binary must not stop the census
            modules.append({"path": str(p), "error": repr(exc)})
            continue
        if rec is None:
            continue
        scanned += 1
        if rec["touches_eos"] or rec.get("is_eos_artifact"):
            modules.append(rec)

    # Fold every module's findings into one census.
    static, delayed, pinvoke, dynamic = {}, {}, {}, {}
    for m in modules:
        if m.get("is_eos_artifact") or "imports_eos" not in m:
            continue
        for s in m["imports_eos"]["static"]:
            static.setdefault(s, []).append(m["path"])
        for s in m["imports_eos"]["delayed"]:
            delayed.setdefault(s, []).append(m["path"])
        for s in m["imports_eos"]["pinvoke"]:
            pinvoke.setdefault(s, []).append(m["path"])
        for s in m["eos_symbol_strings"]:
            dynamic.setdefault(s, []).append(m["path"])

    ref_symbols = set(reference.get("symbols", []))

    def missing(mapping):
        return {s: mods for s, mods in sorted(mapping.items()) if s not in our_exports}

    # Anything named only as a string, and never statically imported, is resolved at run time.
    dynamic_only = {s: m for s, m in dynamic.items() if s not in static and s not in delayed}

    census = {
        "modules_scanned": scanned,
        "modules_touching_eos": len([m for m in modules if m.get("touches_eos")]),
        "imported": {
            "static": sorted(static),
            "delayed": sorted(delayed),
            "pinvoke": sorted(pinvoke),
            "dynamic_only": sorted(dynamic_only),
        },
        "missing": {
            # A static import we do not export: the loader fails and the game never starts.
            "loader_fatal": missing(static),
            # Resolved at first call. The game runs until it needs this.
            "optional_delayed": missing(delayed),
            # Declared by managed code, resolved on first managed call.
            "pinvoke_required": missing(pinvoke),
            # Named in the image but resolved through GetProcAddress/dlsym; a null is the game's
            # to handle, and most do not.
            "dynamic_required": missing(dynamic_only),
        },
    }

    # Per-family totals, against both our surface and the real SDK's.
    families = {}
    all_imported = set(static) | set(delayed) | set(pinvoke) | set(dynamic_only)
    for s in sorted(all_imported | ref_symbols):
        fam = families.setdefault(
            family_of(s), {"reference": 0, "ours": 0, "imported": 0, "missing": 0}
        )
        if s in ref_symbols:
            fam["reference"] += 1
        if s in our_exports:
            fam["ours"] += 1
        if s in all_imported:
            fam["imported"] += 1
            if s not in our_exports:
                fam["missing"] += 1
    census["families"] = dict(sorted(families.items()))

    report = {
        "schema_version": 1,
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "target": str(target),
        "emulator": {"library": args.our_library, "exports": len(our_exports)},
        "reference_sdk": {k: v for k, v in reference.items() if k != "symbols"},
        "census": census,
        "modules": modules,
    }
    Path(args.out).write_text(json.dumps(report, indent=2))

    # A short human summary; the JSON is the artifact.
    fatal = census["missing"]["loader_fatal"]
    print("target            : %s" % target)
    print("modules scanned   : %d (%d touch EOS)" % (scanned, census["modules_touching_eos"]))
    print("our exports       : %d" % len(our_exports))
    if reference:
        print("reference SDK     : %d exports (%s)" % (reference["exports"], reference["path"]))
    print("imported EOS      : static=%d delayed=%d pinvoke=%d dynamic-only=%d" % (
        len(static), len(delayed), len(pinvoke), len(dynamic_only)))
    print("LOADER-FATAL      : %d" % len(fatal))
    for s in sorted(fatal):
        print("    %s   <- %s" % (s, ", ".join(Path(p).name for p in fatal[s])))
    for label in ("pinvoke_required", "optional_delayed", "dynamic_required"):
        miss = census["missing"][label]
        if miss:
            print("%-18s: %d" % (label, len(miss)))
            for s in sorted(miss)[:12]:
                print("    %s" % s)
    print("wrote %s" % args.out)
    return 0 if not fatal else 1


if __name__ == "__main__":
    sys.exit(main())
