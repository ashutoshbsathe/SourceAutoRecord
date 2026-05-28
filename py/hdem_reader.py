import os
import struct

HDEM_MAGIC = 0x4D454448

FIELD_TYPES = {
    0: 'FLOAT',
    1: 'INT32',
    2: 'VEC3',
    3: 'BOOL',
    4: 'STRING',
    5: 'HANDLE',
    6: 'BYTE',
    7: 'SHORT',
    8: 'COLOR',
}

SOLID_TYPES = {
    0: 'SOLID_NONE',
    1: 'SOLID_BSP',
    2: 'SOLID_BBOX',
    3: 'SOLID_OBB',
    4: 'SOLID_SOLID_OBB_YAW',
    5: 'SOLID_CUSTOM',
    6: 'SOLID_VPHYSICS',
}

class HdemReader:
    def __init__(self, path: str):
        self.path = path
        self.file = None
        self.filesize = 0
        self.version = 0
        self.flags = 0
        self.map_name = ""
        self.tickrate = 0.0
        self.timestamp = 0
        self.sar_version = ""
        self.game_dir = ""
        self.schema_offset = 0
        self.classes = {}
        self.fields = {}
        self.ticks_start_pos = 0
        self.current_entities = {} # entity_index -> entity_state dict
        self._open()

    def _read_cstring(self):
        res = bytearray()
        while True:
            b = self.file.read(1)
            if not b or b == b'\x00':
                break
            res.extend(b)
        return res.decode('utf-8', errors='replace')

    def _open(self):
        self.filesize = os.path.getsize(self.path)
        self.file = open(self.path, 'rb')
        try:
            magic, self.version, self.flags = struct.unpack('<IHH', self.file.read(8))
            if magic != HDEM_MAGIC:
                raise ValueError(f"Invalid magic 0x{magic:08X} (expected 0x{HDEM_MAGIC:08X})")
            self.map_name = self._read_cstring()
            self.tickrate, self.timestamp = struct.unpack('<fQ', self.file.read(12))
            self.sar_version = self._read_cstring()
            self.game_dir = self._read_cstring()
            (self.schema_offset,) = struct.unpack('<Q', self.file.read(8))
            self.ticks_start_pos = self.file.tell()

            if self.schema_offset != 0 and self.schema_offset < self.filesize:
                # Seek to schema offset to read classes and fields
                self.file.seek(self.schema_offset)
                (num_classes,) = struct.unpack('<H', self.file.read(2))
                for _ in range(num_classes):
                    (cid,) = struct.unpack('<H', self.file.read(2))
                    cname = self._read_cstring()
                    self.classes[cid] = cname

                (num_fields,) = struct.unpack('<H', self.file.read(2))
                for _ in range(num_fields):
                    (fid,) = struct.unpack('<H', self.file.read(2))
                    fname = self._read_cstring()
                    (ftype,) = struct.unpack('<B', self.file.read(1))
                    self.fields[fid] = (fname, ftype)

            # Seek back to ticks start
            self.file.seek(self.ticks_start_pos)
        except Exception as e:
            self.file.close()
            raise e

    def close(self):
        if self.file:
            self.file.close()
            self.file = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def read_next_tick(self):
        """Reads next tick from file and updates current reconstructed state.
        Returns a tuple of (tick_number, current_entities) or None if EOF.
        """
        if self.file.tell() >= (self.schema_offset or self.filesize):
            return None

        tb = self.file.read(10)
        if len(tb) < 10:
            return None

        tick_number, num_ents, frame_size = struct.unpack('<iHI', tb)
        payload = self.file.read(frame_size)
        if len(payload) < frame_size:
            return None

        ppos = 0
        for _ in range(num_ents):
            ent_idx, serial, cid, eflags, num_f = struct.unpack_from('<HHHBB', payload, ppos)
            ppos += 8

            if eflags & 4: # HDEM_ENT_DELETED
                if ent_idx in self.current_entities:
                    del self.current_entities[ent_idx]
                continue

            if ent_idx not in self.current_entities or (eflags & 8): # HDEM_ENT_FULL_SNAPSHOT
                self.current_entities[ent_idx] = {
                    'entity_index': ent_idx,
                    'serial_number': serial,
                    'class_name': self.classes.get(cid, 'UNKNOWN'),
                    'target_name': '',
                    'position': [0.0, 0.0, 0.0],
                    'angles': [0.0, 0.0, 0.0],
                    'velocity': [0.0, 0.0, 0.0],
                    'fields': {}
                }

            ent = self.current_entities[ent_idx]
            ent['serial_number'] = serial

            for _ in range(num_f):
                (fid,) = struct.unpack_from('<H', payload, ppos)
                ppos += 2

                # Check well-known fields first
                if fid == 11: # HDEM_FIELD_CLASSNAME
                    start = ppos
                    while ppos < len(payload) and payload[ppos] != 0:
                        ppos += 1
                    ent['class_name'] = payload[start:ppos].decode('utf-8', errors='replace')
                    ppos += 1
                elif fid == 12: # HDEM_FIELD_NAME
                    start = ppos
                    while ppos < len(payload) and payload[ppos] != 0:
                        ppos += 1
                    ent['target_name'] = payload[start:ppos].decode('utf-8', errors='replace')
                    ppos += 1
                elif fid == 0: # HDEM_FIELD_ORIGIN
                    vx, vy, vz = struct.unpack_from('<fff', payload, ppos)
                    ent['position'] = [vx, vy, vz]
                    ppos += 12
                elif fid == 1: # HDEM_FIELD_ANGLES
                    vx, vy, vz = struct.unpack_from('<fff', payload, ppos)
                    ent['angles'] = [vx, vy, vz]
                    ppos += 12
                elif fid == 2: # HDEM_FIELD_VELOCITY
                    vx, vy, vz = struct.unpack_from('<fff', payload, ppos)
                    ent['velocity'] = [vx, vy, vz]
                    ppos += 12
                else:
                    fname, ftype = self.fields.get(fid, ('UNKNOWN', 0))
                    val = None
                    if ftype == 0: # FLOAT
                        (val,) = struct.unpack_from('<f', payload, ppos)
                        ppos += 4
                    elif ftype == 1: # INT32
                        (val,) = struct.unpack_from('<i', payload, ppos)
                        ppos += 4
                    elif ftype == 2: # VEC3
                        vx, vy, vz = struct.unpack_from('<fff', payload, ppos)
                        val = [vx, vy, vz]
                        ppos += 12
                    elif ftype == 3: # BOOL
                        (val,) = struct.unpack_from('<B', payload, ppos)
                        val = True if val else False
                        ppos += 1
                    elif ftype == 4: # STRING
                        start = ppos
                        while ppos < len(payload) and payload[ppos] != 0:
                            ppos += 1
                        val = payload[start:ppos].decode('utf-8', errors='replace')
                        ppos += 1
                    elif ftype == 5: # HANDLE
                        (val,) = struct.unpack_from('<I', payload, ppos)
                        ppos += 4
                    elif ftype == 6: # BYTE
                        (val,) = struct.unpack_from('<B', payload, ppos)
                        ppos += 1
                    elif ftype == 7: # SHORT
                        (val,) = struct.unpack_from('<h', payload, ppos)
                        ppos += 2
                    elif ftype == 8: # COLOR
                        r, g, b, a = struct.unpack_from('<BBBB', payload, ppos)
                        val = [r, g, b, a]
                        ppos += 4

                    ent['fields'][fname] = (ftype, val)

        # Return a copy of reconstructed entities for this tick
        import copy
        return tick_number, copy.deepcopy(self.current_entities)

    def __iter__(self):
        while True:
            res = self.read_next_tick()
            if res is None:
                break
            yield res
