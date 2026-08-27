#!/bin/bash
# Double-click in Finder: GDOS 2.4, Sopp EPROM, OMTI controller.
# Thin wrapper -- all the actual configuration lives in run-multihdc.command.
cd "$(dirname "$(readlink "$0")")"
exec ./run-multihdc.command calvados

# cd "$(dirname "$0")"
# exec ./run-multihdc.command gdos