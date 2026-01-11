# GhostInject

A lightweight DLL injector with a clean UI and multiple injection methods.


<img width="531" height="438" alt="image" src="https://github.com/user-attachments/assets/f6f8e8ab-a34a-4d8e-a1d6-53ee6b1dec93" />
<br> 
<img width="1063" height="497" alt="Screenshot 2026-01-11 132226" src="https://github.com/user-attachments/assets/9455d205-1cbf-422d-9860-d6e4d0861914" />
<br> 
<img width="531" height="440" alt="image" src="https://github.com/user-attachments/assets/09b49984-bb82-41fc-88d4-194d58f7a3a7" />

## Features

- **Process Selection** - Browse and filter all running processes
- **Three Injection Methods**
  - Standard LoadLibrary injection
  - Manual map injection (stealthier)
  - Manual map with XOR encryption (most stealth)
- **Real-time Logging** - Track every step of the injection
- **Clean Interface** - Simple two-panel design, no clutter

## How It Works

### Standard Injection
Uses the classic `LoadLibraryA` method. Fast and reliable for most scenarios.

### Manual Map
Manually maps the DLL into memory without using Windows loader. Harder to detect by basic anti-cheat systems.

### Manual Map + Encryption
Encrypts executable sections with XOR before mapping, then decrypts at runtime. Adds another layer of stealth.

## Usage

1. Click **Select Process** and choose your target
2. Click **Pick** to select your DLL file
3. Choose injection method
4. Hit **INJECT**

## Building

Requires:
- Visual Studio 2019+
- DirectX 11 SDK
- ImGui (included)

Just open the solution and build. All dependencies are included.

## Structure

```
GhostInject/
├── core/
│   ├── header/
│   │   └── Injector.h       # Injection interface
│   └── source/
│       └── Injector.cpp     # Injection logic
├── imgui/                   # UI library
├── Main.cpp                 # UI implementation
└── resource.h               # App resources
```

## Technical Details

- Written in C++
- Uses ImGui for UI
- DirectX11 for rendering
- Supports 64-bit processes only
- No external DLL dependencies

## Disclaimer

This tool is for educational purposes and legitimate testing only. Using it on games or software without permission may violate terms of service or laws. Use responsibly.

## Author

Created by Pratham Naik - 2026

---

*If you find this useful, drop a star ⭐*
