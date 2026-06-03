import struct
import sys
import os
from p2harness.harness_pb2 import RolloutHeader, RolloutStep


def read_delimited_proto(file_path):
    if not os.path.exists(file_path):
        print(f'Error: File {file_path} not found.')
        return

    with open(file_path, 'rb') as f:
        # Read Header first
        size_data = f.read(4)
        if not size_data:
            print('Error: Empty file.')
            return

        size = struct.unpack('<I', size_data)[0]
        header = RolloutHeader()
        header.ParseFromString(f.read(size))

        print('=== Rollout Header ===')
        print(f'Map:      {header.map_name}')
        print(f'SHM Res:  {header.shm_width}x{header.shm_height}')
        print(f'Tickrate: {header.tickrate:.2f}')
        print('======================\n')

        count = 0
        image_count = 0
        entity_snapshot_count = 0
        total_entities_seen = 0
        non_zero_mouse_count = 0
        any_button_pressed_count = 0
        unique_entity_classes = set()

        while True:
            size_data = f.read(4)
            if not size_data:
                break

            size = struct.unpack('<I', size_data)[0]
            step = RolloutStep()
            step.ParseFromString(f.read(size))

            if step.image_data:
                image_count += 1

            if step.state.HasField('entity_snapshot'):
                snapshot = step.state.entity_snapshot
                if snapshot.entities:
                    entity_snapshot_count += 1
                    total_entities_seen += len(snapshot.entities)
                    for ent in snapshot.entities:
                        unique_entity_classes.add(ent.class_name)

            act = step.action
            if act.mouse_dx != 0.0 or act.mouse_dy != 0.0:
                non_zero_mouse_count += 1

            if (
                act.key_forward
                or act.key_backward
                or act.key_left
                or act.key_right
                or act.key_use
                or act.key_zoomin
                or act.key_zoomout
                or act.key_crouch
                or act.portal_primary
                or act.portal_secondary
                or act.key_jump
            ):
                any_button_pressed_count += 1

            if count < 10:
                btn_str = f'F:{int(act.key_forward)} B:{int(act.key_backward)} L:{int(act.key_left)} R:{int(act.key_right)} J:{int(act.key_jump)}'
                num_ents = (
                    len(step.state.entity_snapshot.entities)
                    if step.state.HasField('entity_snapshot')
                    else 0
                )
                has_img = 'YES' if step.image_data else 'NO'
                print(
                    f'Step {count:03d} | Tick: {step.state.server_tick:6d} | '
                    f'Pos: ({step.state.position.x:8.2f}, {step.state.position.y:8.2f}, {step.state.position.z:8.2f}) | '
                    f'Buttons: {btn_str} | Ents: {num_ents:3d} | Img: {has_img}'
                )

            if step.image_data and image_count == 1:
                print(
                    f'\n[!] Found image data ({len(step.image_data)} bytes). Saving to first_frame.raw...'
                )
                with open('first_frame.raw', 'wb') as img_file:
                    img_file.write(step.image_data)
                print(
                    "Tip: Use 'ffplay -f rawvideo -pixel_format rgb24 -video_size 854x480 first_frame.raw' to view.\n"
                )

            count += 1

        print(f'\nFinished. Total steps: {count}')
        print(f'Steps with pixel data: {image_count} / {count}')
        print(f'Steps with entity snapshots: {entity_snapshot_count} / {count}')
        if entity_snapshot_count > 0:
            print(
                f'Average entities per snapshot: {total_entities_seen / entity_snapshot_count:.1f}'
            )
            print(
                f'Unique entity classes tracked: {sorted(list(unique_entity_classes))}'
            )
        print(
            f'Steps with active action/movement keys: {any_button_pressed_count} / {count}'
        )
        print(f'Steps with non-zero mouse inputs: {non_zero_mouse_count} / {count}')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: python validate_rollout.py <path_to_rollout>')
    else:
        # Ensure we can find the p2harness package
        sys.path.append(os.path.dirname(os.path.abspath(__file__)))
        read_delimited_proto(sys.argv[1])
