# 🎮 Steam Account Switcher — baston.dev

A lightweight and fast GUI written in **C++ with ImGui + DirectX11** to switch between multiple Steam accounts in a single click.

Perfect for users managing several Steam accounts for **CS2**, **Dota 2**, or any other game on the platform.

---

## ✨ Features

- 🔍 Automatically detects accounts from the `loginusers.vdf` file
- 🖼️ Loads avatars from Steam's avatar cache
- ✅ Auto-login support via Windows registry modification (`AutoLoginUser` and `RememberPassword`)
- 📋 Optional alphabetical sorting
- 🔄 Button to **refresh the account list**
- ➕ **Add new login** feature (launch Steam without an active session)
- ❌ Automatically kills the following processes:
  - `steam.exe`
  - `steamservice.exe`
  - `steamwebhelper.exe`
  - `cs2.exe`

---

## 📦 Download

👉 [**Download latest compiled version (.exe)**](https://github.com/bastontheking/steam-account-switcher/releases)


---

## 🛠️ Build Instructions

This project uses:
- Visual Studio 2022+
- Windows 10+ SDK
- ImGui with DirectX11 backend (already included in the project)

Make sure you have the DirectX SDK properly configured on your system.
