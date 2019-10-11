#!/usr/bin/python

import re
import sys
import os

# check command line arguments
if (len(sys.argv)!=2):
  print "Usage: " + sys.argv[0] + " input file"
  sys.exit(1)
# check if file exists
file_exist = os.path.isfile(sys.argv[1])
if (file_exist):
  file_name = sys.argv[1]
else:
  print "Error file " + sys.argv[1] + " doesn't exist"

# open file
file_desc = open(file_name, "r")

# regular expressions for parsing
#numa_pattern = re.compile("NUMANode P#([0-9]+) ") # this pattern doesn't appear in case of a single NUMA
package_pattern = re.compile("Package[ \t]+P#([0-9]+)")
core_pattern = re.compile("Core[ \t]+P#([0-9]+)")
thread_pattern = re.compile("PU[ \t]+P#([0-9]+)")

#cur_numa = -1;
cur_package = -1;
cur_core = -1;
cur_thread = -1;

# scanning file
for line in file_desc:
  #numa_match = numa_pattern.search(line)
  #if numa_match:
  #  cur_numa = numa_match.group(1)
  #  continue
  package_match = package_pattern.search(line)
  if package_match:
    cur_package = package_match.group(1)
    continue
  core_match = core_pattern.search(line)
  if core_match:
    cur_core = core_match.group(1)
    continue
  thread_match = thread_pattern.search(line)
  if thread_match:
    cur_thread = thread_match.group(1)
    #if cur_numa == -1 or cur_core == -1:
    #  print "Error cur_numa " + str(cur_numa) + " cur_core " + str(cur_core) + " on line " + str(line)
    #  continue
    #print cur_thread + " " + cur_core + " " + cur_numa
    if cur_package == -1 or cur_core == -1:
      print "Error cur_package " + str(cur_package) + " cur_core " + str(cur_core) + " on line " + str(line)
      continue
    print cur_thread + " " + cur_core + " " + cur_package

