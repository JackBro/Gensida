# Gensida
**IDA** debugger plugin for **Sega Genesis** / **Megadrive** ROMs based on **Gens ReRecordings** emulator modifications.

## How to compile
1. Open **gens_vc10.vcxproj** in any text editor and edit paths to **IDA_DIR** and **IDA_SDK** (version >= *6.8*) accordingly to your real paths;
2. Use **Visual Studio 2015** or newer to compile.

## How to use
1. It's recommended to use **[smd_ida_tools](https://github.com/lab313ru/smd_ida_tools)**;
2. Remove (if any) **Messida** plugin!;
3. Put **plw**-file into **IDA**'s (version >= *6.8*) **plugins** directory;
4. Open your **ROM** in **IDA**, and select **GensIDA**;
5. Debug and enjoy.
