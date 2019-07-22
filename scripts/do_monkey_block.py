import argparse
import json
import os
import signal
import subprocess
import sys
import tempfile
import time

# relative path to the programs
programs_directory = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), 'build', 'release')

# parse the arguments
parser = argparse.ArgumentParser(description='Calibrate, then record responses to a block of video clips.')
parser.add_argument('parameters', help='path to the block parameters JSON file')
parser.add_argument('-f', '--force', help='overwrite output files if they exist', action='store_true')
parser.add_argument('-g', '--show-gpu', help='do not hide the GPU messages', action='store_true')
arguments = parser.parse_args()

# forward-check the parameters
with open(arguments.parameters) as parameters_file:
    parameters = json.load(parameters_file)
if not type(parameters) is dict:
    raise Exception('\'' + arguments.parameters + '\' must contain a JSON object')
for key, expected_type in (('clips', list), ('output', str)):
    if not key in parameters:
        raise Exception('\'' + arguments.parameters + '\' must have a \'' + key + '\' key')
    if not type(parameters[key]) is expected_type:
        raise Exception('\'' + key + '\' of \'' + arguments.parameters + '\' must be a ' + expected_type.__name__)
if len(parameters['clips']) == 0:
    raise Exception('\'clips\' of \'' + arguments.parameters + '\' is empty')
for index in range(0, len(parameters['clips'])):
    if not os.path.isabs(parameters['clips'][index]):
        parameters['clips'][index] = os.path.join(
            os.path.dirname(os.path.realpath(arguments.parameters)),
            parameters['clips'][index])
    with open(parameters['clips'][index]) as clip_file:
        pass
if not os.path.isabs(parameters['output']):
    parameters['output'] = os.path.join(
        os.path.dirname(os.path.realpath(arguments.parameters)),
        parameters['output'])
exists = False
try:
    with open(parameters['output']) as clip_file:
        exists = True
        if not arguments.force:
            raise Exception('\'' + parameters['output'] + '\' already exists (use --force to overwrite it)')
except OSError:
    try:
        with open(parameters['output'], 'w') as clip_file:
            pass
        if not exists:
            os.remove(parameters['output'])
    except OSError:
        raise Exception('\'' + parameters['output'] + '\' could not be open for writing')

record = subprocess.Popen([os.path.join(programs_directory, 'monkey_record')]
    + parameters['clips']
    + [parameters['output']]
    + (['--force'] if arguments.force else []),
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT)
while True:
    line = record.stdout.readline()
    if len(line) == 0:
        break
    if arguments.show_gpu or not line.startswith((b'TVMR', b'NvMM', b'--->', b'Allocating', b'OPENMAX')):
        sys.stdout.buffer.write(line)
        sys.stdout.flush()
