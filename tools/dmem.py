"""Read (and, explicitly, write) Dolphin's emulated GameCube MEM1 from outside the process.
Finds the 32 MiB MEM_MAPPED view whose first 6 bytes are the game ID (like Dolphin Memory Engine).
CLI: python dmem.py r32 <addr> [n] | rf <addr> [n] | dump <addr> <len> <file> | find"""
import ctypes, ctypes.wintypes as wt, struct, subprocess, sys

PROCESS_VM_READ, PROCESS_VM_WRITE, PROCESS_VM_OPERATION, PROCESS_QUERY_INFORMATION = 0x10, 0x20, 0x8, 0x400
MEM_MAPPED = 0x40000
k32 = ctypes.WinDLL("kernel32", use_last_error=True)


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD), ("RegionSize", ctypes.c_size_t),
                ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]


k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.argtypes = k32.ReadProcessMemory.argtypes
k32.OpenProcess.restype = wt.HANDLE


def dolphin_pid():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Dolphin.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith('"Dolphin.exe"'):
            return int(line.split('","')[1])
    raise SystemExit("Dolphin not running")


class Mem:
    def __init__(self, write=False, game_id=b"GCDP08"):
        acc = PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | (PROCESS_VM_WRITE | PROCESS_VM_OPERATION if write else 0)
        self.h = k32.OpenProcess(acc, False, dolphin_pid())
        self.base = None
        mbi, addr = MBI(), 0
        while k32.VirtualQueryEx(self.h, addr, ctypes.byref(mbi), ctypes.sizeof(mbi)):
            if mbi.Type == MEM_MAPPED and mbi.RegionSize == 0x2000000 and mbi.State == 0x1000:
                if self._raw(mbi.BaseAddress, 6) == game_id:
                    self.base = mbi.BaseAddress
                    break
            addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if self.base is None:
            raise SystemExit("MEM1 not found (game not booted?)")

    def _raw(self, p, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        k32.ReadProcessMemory(self.h, p, buf, n, ctypes.byref(got))
        return buf.raw[:got.value]

    def read(self, a, n):
        return self._raw(self.base + (a & 0x01FFFFFF), n)

    def write(self, a, data):
        got = ctypes.c_size_t()
        if not k32.WriteProcessMemory(self.h, self.base + (a & 0x01FFFFFF), data, len(data), ctypes.byref(got)):
            raise OSError(ctypes.get_last_error())

    def u32(self, a):
        return struct.unpack(">I", self.read(a, 4))[0]

    def f32s(self, a, n):
        return struct.unpack(f">{n}f", self.read(a, 4 * n))

    def u32s(self, a, n):
        return struct.unpack(f">{n}I", self.read(a, 4 * n))


if __name__ == "__main__":
    m = Mem()
    c = sys.argv[1]
    if c == "find":
        print(hex(m.base))
    elif c == "r32":
        a, n = int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 1
        for i, v in enumerate(m.u32s(a, n)):
            print(f"{a + 4 * i:08X}: {v:08X}")
    elif c == "rf":
        a, n = int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 1
        print(" ".join(f"{v:.4f}" for v in m.f32s(a, n)))
    elif c == "dump":
        open(sys.argv[4], "wb").write(m.read(int(sys.argv[2], 16), int(sys.argv[3], 16)))
