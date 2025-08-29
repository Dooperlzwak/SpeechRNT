---
name: Bug report
about: Create a report to help us improve SpeechRNT
labels: bug
assignees: ''
---

### Summary
A clear and concise description of the problem.

### Affected area(s)
- [ ] Frontend (`frontend/`)
- [ ] Backend (`backend/`)
- [ ] Documentation (`README.md`, `TESTING.md`, `backend/docs/`)
- [ ] Tooling / Scripts (`scripts/`)
- [ ] CI/CD / Docker

### Environment
- OS and version:
- Docker: [Yes/No]
- Node.js version (if frontend):
- npm version (if frontend):
- Browser and version (if frontend):
- CMake version (if backend):
- Compiler and version (GCC/Clang/MSVC) (if backend):
- GPU model and VRAM (if applicable):
- CUDA toolkit version (if applicable):
- Commit hash (run `git rev-parse HEAD`):

### Versions/Configs
- Relevant config files changed? (`backend/config/*.json`): [Yes/No] (attach diffs if yes)
- Using Docker dev scripts? (`./scripts/docker-dev.*`): [Yes/No]

### Steps to reproduce
1. 
2. 
3. 

Minimal repro is ideal. Please include exact commands if possible, e.g.:
```bash
# Frontend
cd frontend && npm install && npm run dev

# Backend
cd backend && mkdir -p build && cd build
cmake .. -DENABLE_TESTING=ON && make -j$(nproc)
./SpeechRNT
```

### Expected behavior
What you expected to happen.

### Actual behavior
What actually happened. Include full error messages.

### Logs and artifacts
- Frontend console output (and Network tab if relevant)
- `test_results/*.log` (from `scripts/run-all-tests.*`)
- `ctest` output (`cd backend/build && ctest --output-on-failure`)
- Crash dumps or stack traces

### Screenshots / recordings
If applicable, add screenshots or short screen recordings.

### Impact severity
- [ ] Critical (crash/data loss/security)
- [ ] High (major feature broken)
- [ ] Medium (workaround exists)
- [ ] Low (minor or cosmetic)

### Additional context
Add any other context about the problem here.

---
By submitting this issue, you agree to follow our [Code of Conduct](../CODE_OF_CONDUCT.md).

