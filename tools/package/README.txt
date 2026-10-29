McUnreal
========

A game that plays like Minecraft Java Edition 26.3, made with Unreal Engine.
It has none of Minecraft's pictures, sounds or data in it. On its first run it
makes its own copy from YOUR installation of Minecraft, on your PC only.


What you need
-------------
1. Windows 10 or 11, 64-bit, and a graphics card from the last ~8 years (DirectX 11 or 12).
2. Minecraft Java Edition, version 26.3, installed with the official Minecraft
   Launcher (or Prism Launcher). Open the launcher, pick 26.3 and press Play
   once, all the way to the Minecraft title screen, then quit it. That makes
   the launcher download the game, its sounds and its Java, which this game
   reads. You do not need Python, Unreal, mods or anything else.
3. If the game will not start and mentions VCRUNTIME140 or MSVCP140, run
   Windows\Engine\Extras\Redist\en-us\vc_redist.x64.exe once
   (Microsoft's Visual C++ runtime, included here).


Starting it
-----------
Unzip the whole folder somewhere you can write to (for example your Desktop;
not "Program Files") and double-click Play.bat. Don't run it from inside the zip.

The first time, the game shows "First-time setup". Press "Set up". It looks for
Minecraft in the usual places (%APPDATA%\.minecraft and Prism's folder). If
yours is somewhere else, click the box and type or paste (Ctrl+V) the folder
first. The bar fills in about 5 to 10 minutes, once
(it copies about 600 MB; leave the game open and don't let the PC sleep). The copy is kept in
%LOCALAPPDATA%\McUnreal\cache and is not part of this folder. If it says
something is missing, it tells you what to do (nearly always: press Play on
26.3 in the launcher once more) and "Try again" continues.


Playing alone
-------------
Singleplayer -> Create New World -> Create. Move with WASD, Space to jump,
E for the inventory, Esc for the menu. Worlds are saved in this folder under
Windows\McClient\Saved\Worlds.


Hosting a world for a friend
----------------------------
1. Singleplayer. On the world list (and on Create New World) turn the button at
   the top right to "Friends can join: On", then play the world as usual.
2. Windows will ask whether to allow McClient through the firewall the first
   time. Tick "Private networks" (same home network) and also "Public
   networks" if your friend is joining over the internet, then Allow.
3. Tell your friend your address:
   - Same home network (LAN): your PC's local address, for example
     192.168.1.23. Find it with  ipconfig  in a command prompt (IPv4 Address).
   - Over the internet: your public IP address (search "what is my IP"), and
     your router must forward TCP and UDP port 25565 to your PC. Your friend
     may have to be told the port: 203.0.113.5:25565.
   The game uses port 25565 (the same number as Minecraft) but only talks to
   other copies of this game, not to real Minecraft.

Joining a friend
----------------
Multiplayer on the title screen. Type your name and their address, then Join
Server. With no port written, 25565 is used. Both of you must have this same
version of the game (the same zip). To try it alone on one PC, run two copies
and join 127.0.0.1.

Leaving the world: Esc -> Save and Quit to Title (host) or Disconnect (guest).
When the host quits, everyone is back at the title screen.
