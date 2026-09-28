"""Serve the local tile-board UI and its native solver endpoint."""

import argparse
import os
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
from urllib.parse import urlsplit


WEB_ROOT = Path(__file__).resolve().parent
REPO_ROOT = WEB_ROOT.parent
MAX_REQUEST_BYTES = 4096
SOLVER_TIMEOUT_SECONDS = 120


def find_solver_binary():
    name = "solver-web-api.exe" if os.name == "nt" else "solver-web-api"
    candidates = [
        REPO_ROOT / "build" / "Release" / name,
        REPO_ROOT / "build" / name,
        REPO_ROOT / "out" / "build" / "Release" / name,
        REPO_ROOT / "out" / "build" / name,
    ]
    for pattern in (
        "out/build/*/Release/" + name,
        "out/build/*/" + name,
        "cmake-build-*/Release/" + name,
        "cmake-build-*/" + name,
        "build/*/Release/" + name,
        "build/*/" + name,
    ):
        candidates.extend(sorted(REPO_ROOT.glob(pattern)))
    return next((path for path in candidates if path.is_file()), None)


class SolverHandler(SimpleHTTPRequestHandler):
    solver_binary = None

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB_ROOT), **kwargs)

    def _json(self, status, payload):
        encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(encoded)

    def do_POST(self):
        if urlsplit(self.path).path != "/api/solve":
            self._json(404, {"error": "Unknown endpoint"})
            return
        try:
            size = int(self.headers.get("Content-Length", ""))
        except ValueError:
            self._json(411, {"error": "A valid Content-Length is required"})
            return
        if not 0 < size <= MAX_REQUEST_BYTES:
            self._json(413 if size > MAX_REQUEST_BYTES else 400,
                       {"error": "Request body must be between 1 and 4096 bytes"})
            return
        if self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower() != "application/json":
            self._json(415, {"error": "Content-Type must be application/json"})
            return
        try:
            payload = json.loads(self.rfile.read(size))
        except (ValueError, UnicodeDecodeError):
            self._json(400, {"error": "Invalid JSON request body"})
            return
        board = payload.get("board") if isinstance(payload, dict) else None
        if (not isinstance(board, list) or len(board) != 9 or
                any(not isinstance(row, str) or len(row) != 7 or
                    any(tile not in "PBGO." for tile in row) for row in board)):
            self._json(400, {"error": "board must be 9 rows of exactly 7 PBGO. characters"})
            return
        binary = self.solver_binary
        if binary is None:
            self._json(503, {"error": "solver-web-api binary not found; build it or pass --solver-binary"})
            return
        try:
            result = subprocess.run([str(binary)], input="\n".join(board) + "\n",
                                    text=True, capture_output=True,
                                    timeout=SOLVER_TIMEOUT_SECONDS, check=False)
        except subprocess.TimeoutExpired:
            self._json(504, {"error": "Solver timed out"})
            return
        except OSError as error:
            self._json(502, {"error": f"Could not start solver: {error}"})
            return
        if result.returncode != 0:
            self._json(502, {"error": result.stderr.strip() or
                                    f"Solver exited with status {result.returncode}"})
            return
        try:
            solution = json.loads(result.stdout)
        except ValueError:
            self._json(502, {"error": "Solver returned invalid JSON"})
            return
        self._json(200, solution)

    def translate_path(self, path):
        candidate = Path(super().translate_path(path)).resolve()
        try:
            candidate.relative_to(WEB_ROOT)
        except ValueError:
            return str(WEB_ROOT / "__forbidden_path__")
        return str(candidate)

    def list_directory(self, path):
        self.send_error(403, "Directory listing disabled")
        return None


def main():
    parser = argparse.ArgumentParser(description="Local tile solver web frontend")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--solver-binary", type=Path)
    args = parser.parse_args()
    SolverHandler.solver_binary = (args.solver_binary.resolve() if args.solver_binary
                                   else find_solver_binary())
    with ThreadingHTTPServer(("127.0.0.1", args.port), SolverHandler) as server:
        print(f"Ready at http://127.0.0.1:{server.server_port}/", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
