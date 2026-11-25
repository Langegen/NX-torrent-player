#!/usr/bin/env python3
import os
import sys
import urllib.request
import zipfile
import shutil

SDK_DIR = os.path.expanduser(r"~\AppData\Local\Android\Sdk")
CMDLINE_TOOLS_URL = "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip"

def setup():
    print(f"Setting up Android SDK at: {SDK_DIR}")
    os.makedirs(SDK_DIR, exist_ok=True)

    # 1. Write licenses
    licenses_dir = os.path.join(SDK_DIR, "licenses")
    os.makedirs(licenses_dir, exist_ok=True)
    
    known_licenses = {
        "android-sdk-license": "\n24333f8a63b6825ea9c5514f83c2829b004d1fee\nd56f5187479451eabf01fb78af6dfcb131a6481e\n24333f8a63b6825ea9c5514f83c2829b004d1fee\n84831b9409646a918e30573bab4c9c91346d8abd",
        "android-sdk-preview-license": "\n84831b9409646a918e30573bab4c9c91346d8abd\n",
        "android-googletv-license": "\n601085b94cd77f0b54ff864069570991500085cd\n",
        "android-sdk-arm-dbt-license": "\n859f317696f97ef38c4791a07250875e516064d8\n",
        "google-gdk-license": "\n33b6a2b64607f11b759f320e80179f33f6ac04ea\n",
        "mips-android-sysimage-license": "\ne9acab5b5fbb560a72cfa4f27c30f4d10488f578\n"
    }
    for lic_file, content in known_licenses.items():
        with open(os.path.join(licenses_dir, lic_file), "w", encoding="utf-8") as f:
            f.write(content)
    print("Licenses configured.")

    # 2. Check if cmdline-tools exist
    latest_dir = os.path.join(SDK_DIR, "cmdline-tools", "latest")
    sdkmanager_bat = os.path.join(latest_dir, "bin", "sdkmanager.bat")

    if not os.path.exists(sdkmanager_bat):
        zip_path = os.path.join(SDK_DIR, "cmdline-tools.zip")
        print(f"Downloading Android Command-line tools from {CMDLINE_TOOLS_URL}...")
        
        def reporthook(count, block_size, total_size):
            if count % 1000 == 0 and total_size > 0:
                percent = int(count * block_size * 100 / total_size)
                print(f"Downloading: {percent}%", end="\r")
        
        urllib.request.urlretrieve(CMDLINE_TOOLS_URL, zip_path, reporthook)
        print("\nDownload complete. Extracting...")
        
        temp_extract = os.path.join(SDK_DIR, "cmdline_temp")
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(temp_extract)
        
        os.remove(zip_path)
        os.makedirs(os.path.dirname(latest_dir), exist_ok=True)
        if os.path.exists(latest_dir):
            shutil.rmtree(latest_dir)
        
        # move temp_extract/cmdline-tools to latest
        shutil.move(os.path.join(temp_extract, "cmdline-tools"), latest_dir)
        shutil.rmtree(temp_extract, ignore_errors=True)
        print(f"Extracted cmdline-tools to: {latest_dir}")
    else:
        print(f"cmdline-tools already exists at: {latest_dir}")

    # 3. Create local.properties in android/
    repo_root = os.path.dirname(os.path.abspath(__file__))
    local_props = os.path.join(repo_root, "android", "local.properties")
    sdk_dir_escaped = SDK_DIR.replace("\\", "\\\\")
    with open(local_props, "w", encoding="utf-8") as f:
        f.write(f"sdk.dir={sdk_dir_escaped}\n")
    print(f"Updated: {local_props}")

if __name__ == "__main__":
    setup()
