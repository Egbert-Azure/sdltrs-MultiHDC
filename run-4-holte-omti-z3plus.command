#!/bin/bash
# Double-click in Finder: Holte CP/M 3.0 on the Z3Plus image, Sopp EPROM, OMTI.
# Thin wrapper -- all the actual configuration lives in run-multihdc.command.
cd "$(dirname "$0")"
exec ./run-multihdc.command z3plus
