# SSD Nuke

> ## ⚠️ WARNING: IRREVERSIBLE DATA LOSS AHEAD ⚠️
> 
> **This tool is provided strictly for research and educational purposes.**  
> **Improper use may result in *permanent* and *irrecoverable* data destruction.**  
> **Proceed only if you fully understand the risks.**

---

## About SSD Nuke 

SSD Nuke is an educational tool demonstrating secure SSD data wiping techniques.  
It offers **3 levels of wipe security**, showing how TRIM operations, file wiping, empty-space sanitization, and Master File Table (MFT) cleaning interact.

` SSDNuke works well with all types of storages such as SSD's(SATA,NVME),HDD's,Thumb drives,SD-Cards ` 
 
---

## Features

- **3 Wipe Levels**  
  1. **Low Security** – Executes `TRIM` + `MFT Destruction`  
  2. **Medium Security** – Executes `TRIM` + `Wipe Existing Files ( 3 Passes ) ` + `MFT Destruction`  
  3. **High Security** – Executes `TRIM` + `Wipe Existing Files ( 3 Passes )` + `Wipe Empty Spaces` + `MFT Destruction`  
- Drive listing and validation before wiping  
- NTFS-only MFT sanitization  
- Final confirmation before destructive actions

---

## Important Warning

- **Running this tool will result in permanent data loss.**  
- **Do NOT run on drives with any important data.**  
- Recovery with forensic tools may be impossible.

---

## Usage

1. Build the project (Windows, Admin rights required).  
2. Run the executable as Administrator.  
3. Select your desired wipe method:  
   - `1` – Low Security  
   - `2` – Medium Security  
   - `3` – High Security
4. Choose the target drive (e.g., `D`).  
5. Confirm when prompted.

---

## Screenshot

![Alt text](https://github.com/NIKJOO/SSDNuke/blob/main/Shot.jpg)

## Follow Me

- **X (Twitter):** [https://x.com/N_Nikjoo](https://x.com/N_Nikjoo)  
- **LinkedIn:** [https://www.linkedin.com/in/nimanikjoo/](https://www.linkedin.com/in/nimanikjoo/)  
- **Telegram Channel:** [https://t.me/VSEC_academy](https://t.me/VSEC_academy)

---
