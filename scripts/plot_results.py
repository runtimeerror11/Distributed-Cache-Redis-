"""Plots CSVs produced by the experiments into results/*.png.
Usage: python3 scripts/plot_results.py results/rehash.csv
"""
import sys

import matplotlib.pyplot as plt
import pandas as pd

# TODO(stage 6): plot modulo vs consistent keys-moved %, and availability/hit-rate over time.
if __name__ == "__main__":
    df = pd.read_csv(sys.argv[1])
    print(df.head())
