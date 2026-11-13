McUnreal dedicated server for Linux
===================================

STATUS: written but NOT built or run yet. Nothing here has been tested on Linux. Treat the paths and
steps as the plan; the first run will show what needs fixing.


What it is
----------
The same game, built for Linux and started with -server (no window, no sound, no graphics). Clients join with
Multiplayer -> your address. Everyone must have the same version of the game as the server.


What you need on the Linux machine
----------------------------------
- 64-bit Linux (x86_64), a recent distribution (Debian 12, Ubuntu 22.04+, Rocky/Alma 8+). 4 GB RAM or more.
- Nothing else installed: no Java, no Unreal. A few libraries the game links (libc, libstdc++, zlib); any normal
  server install has them.
- The "cache": the game's own copy of Minecraft's data (see below).


The cache (read this)
---------------------
The game has none of Minecraft's pictures, sounds or data in it. It reads them from a "cache" folder that it
makes from YOUR installation of Minecraft Java 26.3 on first run (about 600 MB). A Linux server has no Minecraft
install to read, so make the cache on your Windows PC and copy it over:
  1. Run the Windows game once and press "Set up" on the first-time setup screen.
  2. Copy  %LOCALAPPDATA%\McUnreal\cache  to the server as  /opt/mcserver/McClient/cache
     (or any folder, and set MC_CACHE=/that/folder before starting).
This copy is Mojang's data for your own use. Don't put it anywhere public.


Install
-------
  sudo useradd -r -m -d /opt/mcserver mcserver
  sudo unzip McUnreal-Server-Linux-<date>.zip -d /opt/mcserver
  sudo chown -R mcserver: /opt/mcserver
  sudo chmod +x /opt/mcserver/run-server.sh /opt/mcserver/McClient/Binaries/Linux/McClient-Linux-Shipping
Copy the cache in (above), then try it by hand first:
  sudo -u mcserver /opt/mcserver/run-server.sh
It writes the settings on the first start and makes the world, then prints that it is listening. Stop with Ctrl+C.


Settings (McClient/Saved/Server/)
---------------------------------
server.properties  written with defaults on the first start; edit, then restart:
  gamemode=survival         (or creative)
  level-name=world          the world is McClient/Saved/Worlds/<level-name>
  level-seed=               empty = random
  level-type=minecraft:normal
  max-players=20
  motd=A Minecraft Server
  server-port=25565
  white-list=false          true = only names in allowlist.json (and ops) can join
allowlist.json     a JSON list of player names, matched by NAME (there are no accounts), e.g.
                   [{"name":"Alex"},{"name":"Sam"}]
ops.json           operators (can use commands; get past the allowlist and the player limit
                   with "bypassesPlayerLimit"), e.g. [{"name":"Alex","level":4}]
The exact JSON layout is whatever the game wrote on its first start: open the files it created and copy their shape.
-Port=25570 on the command line overrides server-port.
Because names are not checked against accounts, anyone can join under any name unless white-list is on. Turn it
on for a server open to the internet, and don't treat an op name as a secret.


Network and firewall
--------------------
Open the server port (default 25565) for BOTH TCP and UDP:
  ufw:        sudo ufw allow 25565/tcp && sudo ufw allow 25565/udp
  firewalld:  sudo firewall-cmd --permanent --add-port=25565/tcp --add-port=25565/udp && sudo firewall-cmd --reload
  iptables:   sudo iptables -A INPUT -p tcp --dport 25565 -j ACCEPT; sudo iptables -A INPUT -p udp --dport 25565 -j ACCEPT
Behind a home router, forward 25565 TCP and UDP to the server. Cloud hosts: open it in the security group too.
Friends join with  your-address  or  your-address:port.


Run it as a service (systemd)
-----------------------------
  sudo cp /opt/mcserver/mcserver.service /etc/systemd/system/
  sudo systemctl daemon-reload
  sudo systemctl enable --now mcserver
  journalctl -u mcserver -f          the log (it also goes to McClient/Saved/Logs)
  sudo systemctl stop mcserver       saves the world and stops
Edit User, WorkingDirectory and ExecStart in the unit if you installed somewhere else.


Updating
--------
Stop the service, unzip the new zip over /opt/mcserver (worlds and settings are in McClient/Saved, which the zip
doesn't contain), start it. Clients and server must be the same build.


Known open points (from reading the code; check on the first run)
------------------------------------------------------------------
- Linux file names are case-sensitive; the game was built on Windows. A "file not found" for the cache or a
  texture name that differs only in case would show it.
- The first-run setup (the Java data generator) is Windows-only code and isn't used on the server: the server
  never needs "Set up" as long as the cache is copied in.
- This builds the game target and runs it with -server. A true Server target needs an engine built from source,
  which the launcher engine doesn't allow.


Building the zip (on a Windows PC with the toolchain)
-----------------------------------------------------
Needs the UE 5.9 Linux cross-compile toolchain v26_clang-20.1.8-rockylinux8 (about 967 MiB download, from
https://cdn.unrealengine.com/CrossToolchain_Linux/v26_clang-20.1.8-rockylinux8.exe ). Install it, set
LINUX_MULTIARCH_ROOT to its folder (the one holding x86_64-unknown-linux-gnu), then:
  powershell -File tools\server-linux\package-linux-server.ps1 -OutDir I:\mc-package-out
