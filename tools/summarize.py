import os
import sys
import json
import re

directory_name = sys.argv[1]

class PerTest:
    def __init__(self):
        self.good = 0
        self.bad = 0

by_test = {}

sub_count = 0

for submission_name in os.listdir(directory_name):
    if (submission_name.startswith(".")):
        continue
    #print(submission_name)
    sub_count += 1
    file_name = directory_name + "/" + submission_name + "/summary"
    with open(file_name) as json_file:
        data = json.load(json_file)['tests']
        for test_name in data:
            outcome = data[test_name]
            short_name = re.sub(r'_.*','',test_name)
            old = by_test.get(short_name,PerTest())
            if (outcome == "pass"):
                old.good += 1
            else:
                old.bad += 1
            by_test[short_name] = old

#print(by_test)

for test in sorted(by_test.keys()):
    res = by_test[test]
    print(test + " " + str(res.good) + "/" + str(sub_count))
