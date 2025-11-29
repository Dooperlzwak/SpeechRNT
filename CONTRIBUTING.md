## Contributing to Vocr

Thank you for your interest in contributing to Vocr! This document provides guidelines and information to help you contribute effectively., run tests, and submit a high‑quality pull request.

If anything is unclear, open a discussion or an issue — improvements to this document are welcome.

---

### Table of contents
- **Project areas**: see where things live
- **Ways to contribute**: bugs, features, docs
- **Development setup**: Docker, frontend, backend, models
- **Testing**: how to run everything locally
- **Code quality and style**: linting, conventions
- **Branching, commits, and PRs**: workflow expectations
- **Issue reports and feature requests**: what to include
- **Security disclosures**: how to report privately
- **Licensing**: contribution terms

---

### Project areas
- **Frontend**: `frontend/` (React, TypeScript, Vite, Vitest, ESLint)
- **Backend**: `backend/` (C++17, CMake, optional CUDA, GoogleTest/CTest)
- **Scripts & tooling**: `scripts/`
- **Docs**: `README.md`, `TESTING.md`, backend docs in `backend/docs/`

### Ways to contribute
- **Bugs**: report issues with clear reproduction steps
- **Features**: propose enhancements with use cases
- **Docs**: improve clarity, fix inaccuracies, add examples
- **Frontend**: UI/UX, accessibility, performance
- **Backend**: correctness, performance, reliability, tests

Small changes are welcome as individual PRs. For larger proposals, open an issue or discussion first.

---

### Development setup

#### Quick start with Docker (recommended)
The Docker workflow sets up a complete environment (frontend, backend, model downloader):

```powershell
# Windows PowerShell
./scripts/docker-dev.ps1 start
```
```bash
# Linux/macOS
./scripts/docker-dev.sh start
```

Access points:
- Frontend: `http://localhost:3000`
- Backend WebSocket: `ws://localhost:8080`

Use `logs`, `shell`, `build`, `stop`, `clean` subcommands as described in `README.md`.

#### Frontend (React + TypeScript + Vite)
Prereqs: Node.js 18+ and npm

```bash
cd frontend
npm install
npm run dev            # start dev server
npm run build          # production build
npm run lint           # ESLint
npm test               # Vitest (watch)
npm run test:run       # Vitest (single run)
```

#### Backend (C++17 + CMake)
Prereqs: CMake 3.16+, a C++17 compiler (GCC/Clang/MSVC). Optional: CUDA Toolkit 11+ for GPU acceleration.

```bash
cd backend
mkdir -p build && cd build
cmake .. -DENABLE_TESTING=ON
# Linux/macOS
make -j$(nproc)
# Windows (MSVC)
cmake --build . --config Debug
```
Run the server (adjust path/config as needed):
```bash
./SpeechRNT
```

Optional helper scripts:
```bash
# Unix-like
./scripts/build-backend.sh
./scripts/build-frontend.sh

# Windows PowerShell (tests, coverage, lint)
./scripts/run-all-tests.ps1
```

#### Models and large files
- Do not commit model artifacts or large data. The `backend/data/` directory is gitignored.
- Use the provided scripts to download models when needed:
  - `scripts/download-models.sh` (Linux/macOS)
  - `scripts/download-models.py` (cross‑platform)
- Avoid modifying code under `backend/third_party/` unless you are updating vendored dependencies; changes there may be overwritten.

---

### Testing
See `TESTING.md` for full details.

Run the complete test suite and quality checks:
```bash
# Linux/macOS
./scripts/run-all-tests.sh
```
```powershell
# Windows PowerShell
./scripts/run-all-tests.ps1
```

Frontend only:
```bash
cd frontend
npm run test:run        # single run
npm run test:run -- --coverage
```

Backend only (CTest):
```bash
cd backend/build
ctest --output-on-failure
```

Tip: On Windows/MSVC, test executables may appear under `backend/build/Debug/`.

---

### Code quality and style

#### TypeScript/React
- ESLint config is in `frontend/eslint.config.js`.
- Run locally before committing:
  ```bash
  cd frontend
  npm run lint
  npm run test:run
  ```
- Prefer clear, descriptive names and strongly typed public surfaces.
- Keep components small and testable. Co-locate tests in `__tests__/` where applicable.

#### C++
- C++17 is required. Favor clear naming, early returns, and low nesting depth.
- Use guard clauses for errors and handle edge cases first.
- Prefer small, focused translation units to keep build times reasonable.
- Testing is organized under `backend/tests/` with CTest; GoogleTest is used when available, with a simple runner fallback.
- Optional static analysis: if available on your system, run `cppcheck` against `backend/src`.

---

### Branching, commits, and PRs
- Branch from `main` using a descriptive name, e.g. `feature/streaming-retry`, `fix/audio-buffer-overflow`.
- Keep PRs focused and reasonably small; large changes are harder to review.
- Write meaningful commit messages. Conventional Commits are encouraged:
  - `feat(frontend): add performance monitoring dashboard`
  - `fix(backend): prevent buffer overrun in audio ring buffer`
  - `test(frontend): add integration tests for session recovery`
- Include tests for new behavior and bug fixes.
- Update relevant docs when behavior or interfaces change (`README.md`, `TESTING.md`, `backend/docs/*`).
- PR checklist:
  - Builds succeed (frontend and backend)
  - All tests pass locally
  - Lint passes (`frontend`)
  - No accidental large binaries or model files committed

---

### Issue reports and feature requests
Before filing, check existing issues and discussions.

When reporting a bug, please include:
- Expected vs. actual behavior
- Steps to reproduce (minimal repro if possible)
- Environment: OS, Node.js version, compiler/CMake version, GPU/CUDA info if relevant
- Logs: console output, stack traces, or `test_results/*.log` if using the scripts

For features, describe the use case, constraints, and any rough design ideas.

---

### Security disclosures
If you believe you have found a security vulnerability, please do not open a public issue. Instead, submit a private report via GitHub Security Advisories or contact the maintainers through a private channel if available. We’ll coordinate a fix and disclosure.

---

### Licensing
Unless stated otherwise, by submitting a contribution you agree that it will be licensed under the project’s license (see `README.md`). Do not contribute code you do not have the right to license.

---

Thanks again for helping make SpeechRNT better!
