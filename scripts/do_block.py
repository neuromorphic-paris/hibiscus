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
parser.add_argument('-s', '--skip-calibration', help='do not calibration before recording', action='store_true')
parser.add_argument('-g', '--show-gpu', help='do not hide the GPU messages', action='store_true')
parser.add_argument('-e', '--fake-events', help='send fake button pushes periodically', action='store_true')
arguments = parser.parse_args()

# pre_ncurses is used to properly setup the environment when subprocessing a ncurses program.
def pre_ncurses():
    os.setpgrp()
    handler = signal.signal(signal.SIGTTOU, signal.SIG_IGN)
    tty = os.open('/dev/tty', os.O_RDWR)
    os.tcsetpgrp(tty, os.getpgrp())
    signal.signal(signal.SIGTTOU, handler)

# forward-check the parameters
with open(arguments.parameters) as parameters_file:
    parameters = json.load(parameters_file)
if not type(parameters) is dict:
    raise Exception('\'' + arguments.parameters + '\' must contain a JSON object')
for key, expected_type in (('calibration', str), ('clips', list), ('output', str)):
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
for key in ('calibration', 'output'):
    if not os.path.isabs(parameters[key]):
        parameters[key] = os.path.join(
            os.path.dirname(os.path.realpath(arguments.parameters)),
            parameters[key])
    exists = False
    try:
        with open(parameters[key]) as clip_file:
            exists = True
            if key == 'calibration' and arguments.skip_calibration:
                continue
            if not arguments.force:
                raise Exception('\'' + parameters[key] + '\' already exists (use --force to overwrite it)')
    except OSError:
        if key == 'calibration' and arguments.skip_calibration:
            continue
        try:
            with open(parameters[key], 'w') as clip_file:
                pass
            if not exists:
                os.remove(parameters[key])
        except OSError:
            raise Exception('\'' + parameters[key] + '\' could not be open for writing')

# calibrate
if not arguments.skip_calibration:
    with tempfile.NamedTemporaryFile(mode='r+', suffix='.json') as calibrate_parameters_file:
        calibrate_parameters = {}
        for key in ('before_fixation_duration', 'fixation_duration', 'after_fixation_duration' 'points', 'pattern'):
            if key in parameters:
                calibrate_parameters[key] = parameters[key]
        json.dump(calibrate_parameters, calibrate_parameters_file)
        calibrate_parameters_file.flush()
        subprocess.run([
            os.path.join(programs_directory, 'calibrate'),
            parameters['calibration'],
            '--parameters',
            calibrate_parameters_file.name]
            + (['--force'] if arguments.force else []),
            check=True,
            preexec_fn=pre_ncurses)

# record
record = subprocess.Popen([os.path.join(programs_directory, 'record'), parameters['calibration']]
    + parameters['clips']
    + [parameters['output']]
    + (['--force'] if arguments.force else [])
    + (['--fake-events'] if arguments.fake_events else []),
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    preexec_fn=pre_ncurses)
while True:
    line = record.stdout.readline()
    if len(line) == 0:
        break
    if arguments.show_gpu or not line.startswith((b'TVMR', b'NvMM', b'--->', b'Allocating', b'OPENMAX')):
        sys.stdout.buffer.write(line)
        sys.stdout.flush()
