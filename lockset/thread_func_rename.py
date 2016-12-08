#!/usr/bin/python

import os
import re
import sys

#start with some error checking
if len(sys.argv) < 2:
    print("USAGE: thread_func_renamer.py fileOrDir\n")
    sys.exit(1)

arg = sys.argv[1]

if not os.path.exists(arg):
    print("inputted file or directory does not exist\n")
    sys.exit(1)

#regular expression to match whether the line contains a pthread_create call
#yes it is quite complicated but oh well
pattern = re.compile(r'\s*?\w*?\s*?=*\s*?pthread_create(\s*?)\((.+?),(.+?),(.+?),(.+?)\)(\s*?);(\s*?)')
assignment_pattern = re.compile(r'\s*?\w+?\s*?=\s*?pthread_create(\s*?)\((.+?),(.+?),(.+?),(.+?)\)(\s*?);(\s*?)')

def override_line(outfile, line, accum):
    """write out the new output that we want"""
    if assignment_pattern.match(line):
        #assign the assignment to something so compiler won't through warnings
        s = line.split("=", 1)[0].rstrip()
        outfile.write("{0} = 0;\n".format(s))

    #get part within parentheses
    #e.g. get (&threads[t], NULL, PrintHello, (void *)t) out of
    #pthread_create(&threads[t], NULL, PrintHello, (void *)t)
    inside_parens = re.search(r'\(.*\)', line).group(0)

    #split the arguments
    tokens = inside_parens.split(',')
    if len(tokens) != 4:
        print("ERROR: Line doesn't have four tokens: '{}'".format(line))

    func = tokens[2].strip().replace('&', '')
    func_arg = tokens[3].strip()[:-1]

    accum.add(func)

    outfile.write("{0}({1});\n".format(func, func_arg))

def change_functions(old_file, new_file):
    """reads in the file and determines if the line has a pthread_create call"""
    temp_file = "{}.new".format(new_file)
    temp_outfile = open(temp_file, 'w')

    #the accum gets the function names so we can change all of them
    #on the second pass
    accum = set()
    with open(old_file) as f:
        for line in f:
            if pattern.match(line):
                override_line(temp_outfile, line, accum)
            else:
               temp_outfile.write(line)

    temp_outfile.close()

    outfile = open(new_file, 'w')
    with open(temp_file) as f:
        for line in f:
            for s in accum:
                line = line.replace(s, "__{}".format(s))

            outfile.write(line)

    outfile.close()
    os.remove(temp_file)

def process_dir(in_dir, out_dir):
    """get contents of folder and recursively go down the tree to get all files"""
    all = os.listdir(in_dir)
    for i in all:
        if os.path.isfile("{}/{}".format(in_dir, i)):
            #only for certain file extensions
            if i.endswith(".c")  or i.endswith(".cpp"):
                change_functions("{}/{}".format(in_dir, i), "{}/{}".format(out_dir, i))
        else:
            #recurse down the directory
            recurse_dir = "{}/{}".format(out_dir, i)
            os.makedirs(recurse_dir)
            process_dir("{}/{}".format(in_dir, i), recurse_dir)


if os.path.isfile(arg):
    """just create one new file and thats it"""
    (dir, filename) = os.path.split(arg)
    change_functions(arg, "{0}/new_{1}".format(dir, filename))
else:
    """directory so we will create an entirely new directory"""
    (path, dir) = sys.argv[2] or os.path.split(arg)
    out_dir = "{0}/new_{1}".format(path, dir)
    os.makedirs(out_dir)

    process_dir(arg, out_dir)

print("All done!")
