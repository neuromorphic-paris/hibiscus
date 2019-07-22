import argparse
import os
import json
import subprocess
import sys

# relative path to the programs
programs_directory = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), 'third_party', 'hummingbird', 'build', 'release')

# parse the arguments
parser = argparse.ArgumentParser(description='Play multiple stimuli')
parser.add_argument('parameters', help='path to the block parameters JSON file')
parser.add_argument('-g', '--show-gpu', help='do not hide the GPU messages', action='store_true')
arguments = parser.parse_args()

# forward-check the parameters
with open(arguments.parameters) as parameters_file:
    parameters = json.load(parameters_file)
if not type(parameters) is dict:
    raise Exception('\'' + arguments.parameters + '\' must contain a JSON object')
if not 'clips' in parameters:
    raise Exception('\'' + arguments.parameters + '\' must have a \'clips\' key')
if not type(parameters['clips']) is list:
    raise Exception('\'clips\' of \'' + arguments.parameters + '\' must be a list')
if len(parameters['clips']) == 0:
    raise Exception('\'clips\' of \'' + arguments.parameters + '\' is empty')
for index in range(0, len(parameters['clips'])):
    if not os.path.isabs(parameters['clips'][index]):
        parameters['clips'][index] = os.path.join(
            os.path.dirname(os.path.realpath(arguments.parameters)),
            parameters['clips'][index])
    with open(parameters['clips'][index]) as clip_file:
        pass
for key in parameters:
    if key == 'loop':
        if not type(parameters['loop']) is bool:
            raise Exception('\'loop\' of \'' + arguments.parameters + '\' must be a bool')
    if key == 'buffer':
        if not type(parameters['buffer']) is int:
            raise Exception('\'buffer\' of \'' + arguments.parameters + '\' must be an int')
if not 'loop' in parameters:
    parameters['loop'] = False
if not 'buffer' in parameters:
    parameters['buffer'] = 64

# record
record = subprocess.Popen([os.path.join(programs_directory, 'play')]
    + parameters['clips']
    + (['--loop'] if parameters['loop'] else [])
    + ['--buffer', str(parameters['buffer'])],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT)
while True:
    line = record.stdout.readline()
    if len(line) == 0:
        break
    if arguments.show_gpu or not line.startswith((b'TVMR', b'NvMM', b'--->', b'Allocating', b'OPENMAX')):
        sys.stdout.buffer.write(line)
        sys.stdout.flush()
