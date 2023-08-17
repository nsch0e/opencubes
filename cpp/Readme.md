# C++ implementation of opencubes
- uses list representation of coordinates with ones
- hashfunction for coordinate is simple concatination of bytes
- can split problem into threads, but performance can be improved

## usage:
```bash
./cubes -n N
```
### options:
```
-n    --cube_size
the size of polycube to generate up to
This parameter is required.

-t    --threads
the number of threads to use while generating
This parameter is optional. The default value is '1'.

-c    --use_cache
whether to load cache files.
The last N-1 run must have used -w parameter and that process
must have completed without errors. The cache file
must be present under the cache folder. (-f parameter)
This parameter is optional. The default value is '0'.

-w    --write_cache
whether to save cache files
This parameter is optional. The default value is '0'.

-s    --split_cache
whether to save separated cache files per output shape.
requires -w parameter to take affect.
No combined cache file is saved when -s is present.
This parameter is optional. The default value is '0'.

-u    --use_split_cache
whether to load separated cache files per output shape.
The last N-1 run must have used -s parameter and that process
must have completed without errors. The split cache file(s)
must be present under the cache folder. (-f parameter)
This parameter is optional. The default value is '0'.

-f    --cache_file_folder
where to store cache files.
This parameter is optional. The default value is './cache/'.
```

### split cache usage:
Starting with N=9 and beyond it makes sense to use the disk cache system.
To generate starting cache run:
```bash
./cubes -n 9 -w -s
```

Above saves of the results into the cache folder (specified with -f parameter)
as split cache files. Next N=10 run can continue processing from where the last N=9 process stopped:
```bash
./cubes -n 10 -w -s -u
```
The split cache file mode attempts to minimize memory usage.
All following runs can use above command by incrementing the N by one each time.

If required you can merge the split cache files
back into single file at last run by dropping the `-s` parameter.
Merging the split cache this way however uses vastly more memory.
(Tool should be developed to export/merge the split cache files as standard cube format file)

## building (cmake)
To build a release version (with optimisations , default)
```bash
mkdir build && cd build
cmake ..
make
```

To build a Debug version (if you are debugging a change)
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
```