#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Open project and SignalTap GUI
cd "$(dirname "$0")"
quartus servant_zvibe_max10_08_eval_xip.qpf &
sleep 3
quartus_stpw &
