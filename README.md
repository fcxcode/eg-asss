# A Small Subspace Server
A zone server for the multiplayer game subspace.

## Dependencies
* Python 2
* Berkeley DB 4+ libraries
* MySQL or MariaDB client libraries
* GNU make
* GNU Debugger

Ubuntu 10.04 Lucid Lynx:
```
sudo apt-get install build-essential python2.6 python2.6-dev python2.6-dbg libdb4.8-dev mysql-client libmysqlclient-dev gdb mercurial
```

Ubuntu 12.04 Precise Pangolin:
```
sudo apt-get install build-essential python2.7 python2.7-dev python2.7-dbg libdb5.1-dev mysql-client libmysqlclient-dev gdb mercurial
```

Ubuntu 14.04 Trusty Tahr:
```
sudo apt-get install build-essential python2.7 python2.7-dev python2.7-dbg libdb5.3-dev mysql-client libmysqlclient-dev gdb mercurial
```

CentOS 6:
```
sudo yum groupinstall "Development Tools"
sudo yum install python-libs python-devel python-debuginfo db4-devel mysql-libs mysql-devel gdb mercurial
```

## Installing on GNU/Linux
Each step has an example for Ubuntu 14.04 Trusty (64 bit)

1. Install the dependencies listed in the previous section  
   `sudo apt-get install build-essential python2.7 python2.7-dev python2.7-dbg libdb5.3-dev mysql-client libmysqlclient-dev gdb mercurial`
2. Clone/download this repository  
   `hg clone https://bitbucket.org/grelminar/asss ~/asss-src`  
   `cd ~/asss-src`
3. Create src/system.mk using one of the example system.mk.*.dist files  
   `cp ~/asss-src/src/system.mk.trusty.dist ~/asss-src/src/system.mk`
4. Run make in the src directory  
   `cd ~/asss-src/src && make`
5. Copy the dist folder to the location where your zone should live  
   `cp -R ~/asss-src/dist ~/zone`
6. Symlink (or copy) the bin directory into your zone folder  
   `ln -s ~/asss-src/bin ~/zone/bin`
7. Download the correct enc_cont.so file from the [downloads section](downloads) into bin  
   `cd ~/zone/bin`  
   `wget --output-document=enc_cont.so https://bitbucket.org/grelminar/asss/downloads/enc_cont_1.6.0_libc2.11.1_64bit.so`
8. Run "continuum.exe Z" on windows/wine and copy "scrty" and "scrty1" into the zone folder, overwriting the existing files
   You will have to keep these files private, so make sure you close down the file permissions  
    _The path in the example will look like: `~/zone/scrty` and `~/zone/scrty1`_  
   `chmod 0600 ~/zone/scrty*`
9. You can now run the zone by running "bin/asss" in your zone folder  
    `cd ~/zone && bin/asss`
10. Optionally, you can run asss using the `run-asss` script that automatically restarts the zone if it crashes or if a
    sysop uses `?recyclezone` (this command will not work properly without this script)  
    `cp ~/asss-src/scripts/run-asss ~/zone`  
    `nano ~/zone/run-asss` and make sure the line `ASSSHOME=$HOME/zone` is correct  
    `cd ~/zone && ./run-asss`
11. It is also possible to run asss as a service, you can find an example ubuntu init file in the `scripts` directory


## Vagrant
You can automatically set up a virtual machine that runs your zone using vagrant:

1. Install VirtualBox https://www.virtualbox.org/ (or another provider that vagrant supports)
2. Install Vagrant: http://www.vagrantup.com/
3. Run "vagrant up" in the directory with the 'Vagrantfile' from the repository, to set up the VM
4. Run "vagrant ssh" to login
5. Type "runzone" to run the server!

Any time you change the source and you would like to rebuild, run "vagrant provision" on the host (not in the VM)


## Building on windows
Using Visual Studio 2013

1. Install [VS Express 2013 for Windows Desktop](https://www.visualstudio.com/products/visual-studio-express-vs) (this edition is free, other editions should also work)
2. Install [Python 2.7 32 bit](https://www.python.org/downloads/) and make sure python is on your %PATH% (you might need to reboot)
3. Install [Berkeley DB 4.8](http://www.oracle.com/technetwork/database/database-technologies/berkeleydb/downloads/index.html)
4. Install [MySQL Connector/C 32 bit](https://dev.mysql.com/downloads/connector/c/)
5. Download [pthreads win32](https://sourceware.org/pthreads-win32/) and unzip it wherever you like.  
   You need 32-bit (x86) dll, lib and header files. The specific version you need is labelled "VC2"
6. Download the [zlib developer files and binaries](http://gnuwin32.sourceforge.net/packages/zlib.htm) and unzip it wherever you like
7. Clone/download this repository
8. Open "src/asss.sln" in Visual Studio
9. Open the "Property Manager" window _(View -> Other Windows -> Property Manager)_
10. In this window find an entry named "Microsoft.Cpp.Win32.user" _(note that you will find this entry multiple times, but they all point to the same thing)_. And right click it to open "Properties"
11. You will need to find a setting called "Additional **Include** Directories" _(Common Properties -> C/C++ -> General)_. This value should contain all the directories that contain ".h" files of the libraries that were installed/unziped in step 2 - 6. Here is an example:  
    `C:\Program Files (x86)\MySQL\MySQL Connector C 6.1\include;C:\Program Files (x86)\Oracle\Berkeley DB 4.8.30\include;C:\Python27\include;c:\libs\pthread\pthreads.2;c:\libs\zlib\include%(AdditionalIncludeDirectories)`
12. You will need to find a setting called "Additional **Library** Directories" _(Common Properties -> Linker -> General)_. This value should contain all directories that contain ".inc" files of the libraries. Make sure you are using 32 bit versions. Here is an example:  
    `C:\Program Files (x86)\Oracle\Berkeley DB 4.8.30\lib;C:\Program Files (x86)\MySQL\MySQL Connector C 6.1\lib\vs11;C:\Program Files (x86)\MySQL\MySQL Connector C 6.1\lib;C:\Python27\libs;c:\libs\pthread\Pre-built.2\lib\x86;c:\libs\zlib\lib%(AdditionalLibraryDirectories)`
13. You can now build the solution _(Build -> Build Solution)_. This will create .exe and .dll files in the "build" directory.
14. To run your zone copy the "dist" folder to wherever you would like and create a new folder called "bin". Place asss.exe and all the .dll files in this "bin" folder. You will also want to place the dll files of the libraries in here _(libdb48.dll, libmysql.dll, pthreadVC2.dll, zlib1.dll)_

## Documentation
There is more documention in the doc/ directory, however most sections are outdated.
