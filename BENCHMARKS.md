# RLengine 7.2 — Verified Performance Benchmarks

All measurements were recorded on **RLengine 7.2 (`v7.2.0`) Release** using automated GPU-instanced stress tests with VSync disabled and parallel camera frustum culling enabled.

## 🖥️ Test Environment
* **GPU**: NVIDIA GeForce RTX 5060 (7,895 MB Dedicated VRAM, DirectX 12 FL 12_1, SM 6.5)
* **Resolution**: 1600x900
* **Build**: MSVC C++23 (`Release`, `/O2`)
* **VSync**: Disabled (`--stress-test <N> --smoke-test <frames>`)

---

## 📊 ECS + GPU Instanced Stress Test Results

| Active ECS Entities | Visible (Frustum-Culled) | Frames Sampled | Average FPS | Average Frame Time | Last Frame Time |
| ---: | ---: | ---: | ---: | ---: | ---: |
| **50,001** | 1,314 | 120 | **506.86 FPS** | **1.973 ms** | 1.978 ms |
| **100,001** | 1,080 | 120 | **325.46 FPS** | **3.073 ms** | 3.152 ms |
| **1,000,001** | 2,295 | 30 | **46.80 FPS** | **21.367 ms** | 22.744 ms |

---

## 🛠️ Reproduce Locally

```powershell
# 50,000 entities
.\build\bin\Release\RLengine7.2.exe --stress-test 50000 --smoke-test 120

# 100,000 entities
.\build\bin\Release\RLengine7.2.exe --stress-test 100000 --smoke-test 120

# 1,000,000 entities
.\build\bin\Release\RLengine7.2.exe --stress-test 1000000 --smoke-test 30
```
