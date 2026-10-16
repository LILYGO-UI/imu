"""Exercise the complete runtime, including hardware-keyboard focus handling."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def check_startup(executable: Path, orientation: str) -> None:
    environment = os.environ.copy()
    environment.update(
        SDL_VIDEODRIVER="dummy",
        SDL_AUDIODRIVER="dummy",
        SDL_RENDER_DRIVER="software",
        LILYGO_IMU_DEMO="1",
    )
    command = [
        str(executable.resolve()),
        "--width", "568", "--height", "1232",
        "--orientation", orientation,
    ]
    # A file avoids pipe-buffer deadlocks even if startup produces many logs.
    with tempfile.NamedTemporaryFile(mode="w+b", prefix="lilygo-imu-runtime-") as log:
        process = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT)
        failure = None
        try:
            started = time.monotonic()
            ready_at = None
            while True:
                output = Path(log.name).read_bytes()
                status = process.poll()
                now = time.monotonic()
                if status is not None:
                    raise RuntimeError(f"application exited before shutdown (status {status})")
                if ready_at is None:
                    if b"[appkit] UI ready app_id=cc.lilygo.ui.Imu " in output:
                        ready_at = now
                    elif now - started >= 8:
                        raise RuntimeError("application did not report UI ready within 8 seconds")
                elif now - ready_at >= 1:
                    break
                time.sleep(0.02)

            # Keep the loop running after its first frame so the 50 ms input
            # housekeeping timer and the IMU update timer run repeatedly.
            process.terminate()
            try:
                status = process.wait(timeout=3)
            except subprocess.TimeoutExpired as error:
                raise RuntimeError("application did not shut down within 3 seconds") from error
            if status != 0:
                raise RuntimeError(f"application failed during shutdown (status {status})")
        except (OSError, RuntimeError) as error:
            failure = error
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            output = Path(log.name).read_text(encoding="utf-8", errors="replace")

        if failure is not None:
            raise RuntimeError(f"{orientation}: {failure}\n{output}") from failure
        print(f"{orientation}: UI ready, runtime remained alive, clean shutdown")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("orientation", choices=("portrait", "landscape"))
    args = parser.parse_args()
    try:
        check_startup(args.executable, args.orientation)
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
