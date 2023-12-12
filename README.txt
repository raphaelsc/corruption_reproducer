Reproducer for corruption, which is currently known to be triggered by interaction of XFS DIO writes and discards.

Depends on gcc and xfslibs-dev (ubuntu)

To build: run make

To run: sudo ./reproducer.sh (don't forget to update WORKING_DIR in the script)

If corruption manifests, this will be dumped

```
FSX failed; inspect file*.output files in /home/support
```

Then proceed to inspect the log files which will show something as follow if corruption happened:

```
Reading at offset=6160384, size=131072
Writing at offset=6291456, size=131072
Reading at offset=6291456, size=131072
Writing at offset=6422528, size=131072
Reading at offset=6422528, size=131072
Writing at offset=6553600, size=131072
Reading at offset=6553600, size=131072
Writing at offset=6684672, size=131072
Reading at offset=6684672, size=131072
File memory differs at offset=6701056 ('c' != '�')
Some file operations failed
```


