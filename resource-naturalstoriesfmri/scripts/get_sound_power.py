import sys
import re
import numpy as np
import pandas as pd
import librosa
from mvpa2.misc.data_generators import double_gamma_hrf as hrf
import argparse

argparser = argparse.ArgumentParser('''
        Utility for computing sound power metrics from the Natural Stories audio stimuli
''')
argparser.add_argument('files', nargs='+', help='Paths to audio files for processing')
argparser.add_argument('-c', '--convolve', action='store_true', help='Generate an additional column for sound power convolved with the canonical HRF')
args = argparser.parse_args()

DEBUG = False
if DEBUG:
    from matplotlib import pyplot as plt

name = re.compile('^.*/?([^ /])+\.wav *$')
ix2name = ['Boar', 'Aqua', 'MatchstickSeller', 'KingOfBirds', 'Elvis', 'MrSticky', 'HighSchool', 'Roswell', 'Tulips', 'Tourettes']


df = pd.read_csv(sys.stdin, sep=' ', skipinitialspace=True)
power_ix = (df.time * 441).astype('int') # At a sampling rate of 22050 Hz, there are 441 rmse measurements within a 1 second sampling window
power_ix = np.array(power_ix)
docid = np.array(df.docid)
ix2docid = df.docid.unique()
docid2ix = {}
for i in range(len(ix2docid)):
    docid2ix[ix2docid[i]] = i

max_times = {}
for n in ix2name:
    max_times[n] = df[df.docid == n].time.max()

power = {}
max_len = 0
for i in range(len(args.files)):
    path = args.files[i]
    n = ix2name[int(name.match(path).group(1)) - 1]
    sys.stderr.write('\rProcessing audio file "%s" (%d/%d)        ' %(n, i + 1, 10))
    y, sr = librosa.load(path)
    support = np.arange(0, len(y), 50)
    rmse = librosa.feature.rmse(y, hop_length=50)
    rmse = rmse[0]
    power[n] = rmse
    max_len = max(max_len, len(rmse))
    if DEBUG:
        wav, = plt.plot(y[:500000])
        power, = plt.plot(np.arange(0, 500000, 50), rmse[0,:10000])
        # plt.legend([wav, power], ['WAV form', 'Sound power'])
        # plt.show(block=False)
        # raw_input()
        # plt.clf()

sys.stderr.write('\n')

docid_ix = df.docid.map(docid2ix)

# Instantaneous sound power
soundPower = np.zeros(power_ix.shape)

sys.stderr.write('Extracting instantaneous sound power values...\n')
for i in range(power_ix.shape[0]):
    if docid[i] in power:
        if power_ix[i] < len(power[docid[i]]):
            soundPower[i] = power[docid[i]][power_ix[i]]

df['soundPower'] = soundPower

if args.convolve:
    # HRF convolved sound power
    power_padded = []
    tau_padded = []
    for docid in ix2docid:
        power_padded_cur = np.zeros(max_len)
        tau_padded_cur = np.zeros(max_len)
        if docid in power:
            power_padded_cur[-len(power[docid]):] = power[docid]
            tau_padded_cur[-len(power[docid]):] = np.arange(len(power[docid]), dtype='float') / 441.
        power_padded.append(power_padded_cur)
        tau_padded.append(tau_padded_cur)
    power_padded = np.stack(power_padded, axis=0)
    tau_padded = np.stack(tau_padded, axis=0)

    step = 5000
    t = np.array(df.time)[..., None]
    sys.stderr.write('Convolving sound power with canonical HRF...\n')
    soundPowerHRF = []
    for i in range(0, len(t), step):
        sys.stderr.write('\rRows completed: %d/%d' %(i, len(t)))
        sys.stderr.flush()

        doc_ix_cur = docid_ix[i:i+step]
        soundPowerHRF_cur = np.zeros((doc_ix_cur.shape[0],))

        impulse = power_padded[doc_ix_cur]
        tau = tau_padded[doc_ix_cur]
        t_cur = t[i:i+step]

        valid = np.where(tau.sum(axis=1) > 0)
        impulse = impulse[valid]
        tau = tau[valid]
        t_cur = t_cur[valid]

        soundPowerHRF_cur[valid] = np.nan_to_num(impulse * hrf(t_cur - tau)).sum(axis=1) / 441.
        soundPowerHRF.append(soundPowerHRF_cur)

    soundPowerHRF = np.concatenate(soundPowerHRF, axis=0)

    sys.stderr.write('\n')

    df['soundPowerHRF'] = soundPowerHRF

df.to_csv(sys.stdout, sep=' ', na_rep='nan', index=False)
    
