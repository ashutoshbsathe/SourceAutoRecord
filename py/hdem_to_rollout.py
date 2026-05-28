#!/usr/bin/env python3
import sys
import os
import struct
import argparse

# Add current directory to path to allow importing sibling modules
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from hdem_reader import HdemReader
from p2harness.harness_pb2 import RolloutHeader, RolloutStep

def convert_hdem_to_rollout(hdem_path, rollout_path):
    print(f"Opening sidecar HDEM file: {hdem_path}")
    try:
        reader = HdemReader(hdem_path)
    except Exception as e:
        print(f"Error opening/parsing HDEM file: {e}")
        return False

    print(f"Creating output rollout file: {rollout_path}")
    try:
        out_f = open(rollout_path, 'wb')
    except Exception as e:
        print(f"Error opening output file: {e}")
        reader.close()
        return False

    try:
        # Write RolloutHeader
        header = RolloutHeader()
        header.map_name = reader.map_name
        header.shm_width = 854
        header.shm_height = 480
        header.tickrate = reader.tickrate
        header.shm_name = "portal2_harness_framebuffer_0"
        
        serialized_hdr = header.SerializeToString()
        out_f.write(struct.pack('<I', len(serialized_hdr)))
        out_f.write(serialized_hdr)

        total_steps = 0
        total_bytes = 4 + len(serialized_hdr)

        print("Converting ticks...")
        while True:
            tick_res = reader.read_next_tick()
            if tick_res is None:
                break
            
            tick_number, entities = tick_res

            step = RolloutStep()
            state = step.state
            state.server_tick = tick_number
            
            # Find the player entity to populate top-level GameState properties
            player_ent = None
            for ent in entities.values():
                if ent['class_name'] in ('player', 'portal_player'):
                    player_ent = ent
                    break
            
            if player_ent:
                state.position.x = player_ent['position'][0]
                state.position.y = player_ent['position'][1]
                state.position.z = player_ent['position'][2]
                
                state.velocity.x = player_ent['velocity'][0]
                state.velocity.y = player_ent['velocity'][1]
                state.velocity.z = player_ent['velocity'][2]
                
                state.camera.x = player_ent['angles'][0]
                state.camera.y = player_ent['angles'][1]
                state.camera.z = player_ent['angles'][2]
                
                if 'm_iHealth' in player_ent['fields']:
                    state.health = player_ent['fields']['m_iHealth'][1]
                else:
                    state.health = 100
                
                if 'm_fFlags' in player_ent['fields']:
                    flags = player_ent['fields']['m_fFlags'][1]
                    state.is_crouching = bool(flags & (1 << 1)) # FL_DUCKING
                else:
                    state.is_crouching = False
            else:
                state.health = 100
                state.is_crouching = False

            # Populate entity snapshot
            snapshot = state.entity_snapshot
            snapshot.tick = tick_number
            snapshot.is_full_snapshot = True
            
            for ent_idx, ent_data in entities.items():
                proto_ent = snapshot.entities.add()
                proto_ent.entity_index = ent_idx
                proto_ent.serial_number = ent_data['serial_number']
                proto_ent.class_name = ent_data['class_name']
                proto_ent.target_name = ent_data['target_name']
                
                proto_ent.position.x = ent_data['position'][0]
                proto_ent.position.y = ent_data['position'][1]
                proto_ent.position.z = ent_data['position'][2]
                
                proto_ent.angles.x = ent_data['angles'][0]
                proto_ent.angles.y = ent_data['angles'][1]
                proto_ent.angles.z = ent_data['angles'][2]
                
                proto_ent.velocity.x = ent_data['velocity'][0]
                proto_ent.velocity.y = ent_data['velocity'][1]
                proto_ent.velocity.z = ent_data['velocity'][2]
                
                for fname, (ftype, fval) in ent_data['fields'].items():
                    if fname in ('m_vecAbsOrigin', 'm_angAbsRotation', 'm_vecAbsVelocity'):
                        continue
                    proto_field = proto_ent.fields.add()
                    proto_field.name = fname
                    if ftype == 0: # FLOAT
                        proto_field.float_val = float(fval)
                    elif ftype == 1: # INT32
                        val = int(fval)
                        if val >= 0x80000000:
                            val -= 0x100000000
                        proto_field.int_val = val
                    elif ftype == 2: # VEC3
                        proto_field.vec3_val.x = float(fval[0])
                        proto_field.vec3_val.y = float(fval[1])
                        proto_field.vec3_val.z = float(fval[2])
                    elif ftype == 3: # BOOL
                        proto_field.bool_val = bool(fval)
                    elif ftype == 4: # STRING
                        proto_field.string_val = str(fval)
                    elif ftype == 5: # HANDLE
                        val = int(fval)
                        if val >= 0x80000000:
                            val -= 0x100000000
                        proto_field.handle_val = val
                    elif ftype in (6, 7, 8): # BYTE, SHORT, COLOR
                        val = int(fval)
                        if val >= 0x80000000:
                            val -= 0x100000000
                        proto_field.int_val = val

            # Write RolloutStep
            serialized_step = step.SerializeToString()
            out_f.write(struct.pack('<I', len(serialized_step)))
            out_f.write(serialized_step)
            
            total_steps += 1
            total_bytes += 4 + len(serialized_step)

        print(f"Conversion complete. Wrote {total_steps} steps, total size: {total_bytes} bytes.")
        return True
    finally:
        reader.close()
        out_f.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert .hdem to .rollout (entity-only, offline mode).')
    parser.add_argument('hdem_path', help='Path to input .hdem file')
    parser.add_argument('rollout_path', nargs='?', help='Path to output .rollout file (default: input_base.rollout)')
    args = parser.parse_args()

    if not os.path.exists(args.hdem_path):
        print(f"Error: Input file not found: {args.hdem_path}")
        sys.exit(1)

    rpath = args.rollout_path
    if not rpath:
        rpath = os.path.splitext(args.hdem_path)[0] + '.rollout'

    success = convert_hdem_to_rollout(args.hdem_path, rpath)
    sys.exit(0 if success else 1)
