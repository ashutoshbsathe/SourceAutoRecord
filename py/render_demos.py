"""
Portal 2 Demo Rendering Script via SAR gRPC Harness.

Supports rendering a single demo file or batch processing a directory of demo files
using multiple parallel headless game instances.

Usage:
    # Single demo rendering:
    python py/render_demos.py path/to/demo.dem

    # Batch processing using flags:
    python py/render_demos.py --batch_dir=path/to/demos --num_instances=4
"""

import glob
import grpc
import os
import queue
import sys
import threading
import random
import time

from absl import app, flags

from game_launcher import (
    GameInstance,
    DEFAULT_GAMESCOPE_ARGS,
    DEFAULT_GAME_ARGS,
    DEFAULT_STEAM_RUNTIME_SH,
    DEFAULT_PORTAL2_SH,
    DEFAULT_STAGGER_DELAY,
    get_instance_specific_args,
)
from p2harness import P2Harness

FLAGS = flags.FLAGS

flags.DEFINE_string(
    "demo_path",
    None,
    "Path to a single .dem file or directory of .dem files to render.",
)
flags.DEFINE_string(
    "batch_dir",
    None,
    "Directory containing .dem files for batch processing.",
)
flags.DEFINE_string(
    "output_dir",
    None,
    "Directory to save output rollout files. Defaults to the input directory.",
)
flags.DEFINE_integer(
    "num_instances",
    4,
    "Number of parallel game instances for batch mode.",
)
flags.DEFINE_boolean(
    "pixels",
    True,
    "Enable pixel capture during rendering.",
)
flags.DEFINE_string(
    "ext",
    ".rollout",
    "Output file extension (e.g. .rollout or .rolloutf).",
)
flags.DEFINE_float(
    "timeout",
    3600.0,
    "Safety timeout in seconds for a single demo rendering (default: 3600s / 1hr).",
)
flags.DEFINE_boolean(
    "overwrite",
    True,
    "Overwrite existing rollout files.",
)


def render_demo(
    harness: P2Harness,
    demo_path: str,
    output_path: str,
    overwrite: bool = True,
    capture_pixels: bool = True,
) -> str:
    """Renders a single demo file using the assigned game instance harness."""
    demo_path = os.path.abspath(demo_path)
    output_path = os.path.abspath(output_path)

    if os.path.exists(output_path):
        if not overwrite:
            print(f"↳ Output path already exists, skipping: {output_path}")
            return output_path
        try:
            os.remove(output_path)
        except OSError as e:
            print(f"⚠ Warning: Failed to remove existing file {output_path}: {e}")

    try:
        resp = harness.render_demo(
            demo_path=demo_path,
            output_path=output_path,
            capture_pixels=capture_pixels,
            timeout=FLAGS.timeout,
        )
        if not resp.success:
            raise RuntimeError(f"RenderDemo RPC failed: {resp.error_message}")

        print(
            f"↳ Finished rendering: {resp.final_output_path} ({resp.total_bytes:,} bytes / {resp.recorded_ticks} ticks)"
        )
        return resp.final_output_path
    except grpc.RpcError as e:
        if e.code() == grpc.StatusCode.DEADLINE_EXCEEDED:
            print(
                f"⚠ Warning: Demo rendering exceeded timeout ({FLAGS.timeout}s), stopping forcibly."
            )
            try:
                harness.execute_command("sar_harness_stop_rollout")
            except Exception as stop_err:
                print(f"⚠ Warning: Failed to execute stop command: {stop_err}")
        else:
            raise


class HarnessWorker(threading.Thread):
    """Persistent worker thread that exclusively owns a game harness and pulls demos from the central queue."""

    def __init__(
        self,
        worker_id: int,
        harness: P2Harness,
        demo_queue: queue.Queue,
        output_dir: str | None,
        ext: str,
        capture_pixels: bool,
        overwrite: bool,
    ):
        super().__init__(daemon=True)
        self.worker_id = worker_id
        self.harness = harness
        self.demo_queue = demo_queue
        self.output_dir = output_dir
        self.ext = ext
        self.capture_pixels = capture_pixels
        self.overwrite = overwrite

    def run(self):
        while True:
            try:
                demo_path = self.demo_queue.get()
            except Exception:
                break

            try:
                base_name = os.path.splitext(os.path.basename(demo_path))[0]
                out_dir = (
                    self.output_dir
                    if self.output_dir
                    else os.path.dirname(os.path.abspath(demo_path))
                )
                os.makedirs(out_dir, exist_ok=True)
                out_path = os.path.join(out_dir, base_name + self.ext)
                time.sleep(random.randint(1, 10))
                print(f"[Worker {self.worker_id}] Rendering: {demo_path}")
                render_demo(
                    self.harness,
                    demo_path,
                    out_path,
                    overwrite=self.overwrite,
                    capture_pixels=self.capture_pixels,
                )
            except Exception as e:
                print(f"[Worker {self.worker_id}] Error rendering {demo_path}: {e}")
            finally:
                self.demo_queue.task_done()


def main(argv):
    # Collect demos to process
    demos_to_process = []

    # Check positional arguments (argv[1:]) or explicit demo_path flag
    targets = argv[1:]
    if FLAGS.demo_path:
        targets.append(FLAGS.demo_path)

    for target in targets:
        if os.path.isdir(target):
            # If a directory is passed positionally, treat it as a batch folder
            pattern = os.path.join(target, "*.dem")
            demos_to_process.extend(sorted(glob.glob(pattern)))
        elif os.path.isfile(target):
            demos_to_process.append(target)
        else:
            path = target if target.endswith(".dem") else f"{target}.dem"
            if os.path.isfile(path):
                demos_to_process.append(path)
            else:
                print(f"Error: Demo file or directory not found: {target}")
                sys.exit(1)

    # Check explicit batch_dir flag
    if FLAGS.batch_dir:
        if not os.path.isdir(FLAGS.batch_dir):
            print(f"Error: Batch directory not found: {FLAGS.batch_dir}")
            sys.exit(1)
        pattern = os.path.join(FLAGS.batch_dir, "*.dem")
        demos_to_process.extend(sorted(glob.glob(pattern)))

    # Deduplicate while preserving input sequence order
    demos_to_process = list(dict.fromkeys(demos_to_process))

    if not demos_to_process:
        print("Error: No demo files specified or found to process.\n")
        print("Usage examples:")
        print("  python py/render_demos.py path/to/demo.dem")
        print("  python py/render_demos.py --batch_dir=path/to/demos --num_instances=4")
        sys.exit(1)

    print(f"Found {len(demos_to_process)} demo(s) to process.")

    num_instances = min(FLAGS.num_instances, len(demos_to_process))
    capture_pixels = FLAGS.pixels
    ext = FLAGS.ext if FLAGS.ext.startswith(".") else f".{FLAGS.ext}"

    # Populate central demo queue
    demo_queue = queue.Queue()
    for d in demos_to_process:
        demo_queue.put(d)

    instances = []
    harnesses = []

    print(f"\nLaunching {num_instances} game instance(s)...")
    try:
        for i in range(num_instances):
            inst = GameInstance(
                instance_id=i,
                gamescope_args=DEFAULT_GAMESCOPE_ARGS.copy(),
                game_args=DEFAULT_GAME_ARGS.copy() + get_instance_specific_args(i),
                steam_runtime_sh=DEFAULT_STEAM_RUNTIME_SH,
                portal2_sh=DEFAULT_PORTAL2_SH,
            )
            inst.start()
            instances.append(inst)
            if num_instances > 1:
                time.sleep(DEFAULT_STAGGER_DELAY / 5)

        # Base boot wait of 10.0s ideally as requested, plus staggered startup padding
        boot_wait = 10.0 + (num_instances - 1) * (DEFAULT_STAGGER_DELAY / 2)
        print(f"Waiting {boot_wait:.1f}s for instance(s) to boot...")
        time.sleep(boot_wait)

        print("Connecting to gRPC harnesses (with automatic retries)...")
        for i in range(num_instances):
            harness = P2Harness(f"localhost:{50000 + i}")
            connected = False
            last_err = None
            for attempt in range(15):
                try:
                    resp = harness.handshake()
                    print(f"harness.handshake(): {resp}")
                    harnesses.append(harness)
                    connected = True
                    break
                except Exception as e:
                    last_err = e
                    time.sleep(1.0)
            if not connected:
                print(
                    f"⚠ Warning: Instance {i} handshake failed after 15 attempts: {last_err}"
                )

        if not harnesses:
            raise RuntimeError("Could not connect to any game instances.")

        print(
            f"\nStarting batch rendering across {len(harnesses)} persistent harness worker(s)..."
        )
        workers = []
        for i, harness in enumerate(harnesses):
            w = HarnessWorker(
                worker_id=i,
                harness=harness,
                demo_queue=demo_queue,
                output_dir=FLAGS.output_dir,
                ext=ext,
                capture_pixels=capture_pixels,
                overwrite=FLAGS.overwrite,
            )
            w.start()
            workers.append(w)

        # Block until all demos have been rendered
        demo_queue.join()

        print("\n✔ All demos successfully processed!")

    finally:
        print("\nShutting down game instances...")
        for h in harnesses:
            try:
                h.close()
            except Exception:
                pass
        for inst in instances:
            try:
                inst.stop()
            except Exception:
                pass
        print("Done.")


if __name__ == "__main__":
    app.run(main)
