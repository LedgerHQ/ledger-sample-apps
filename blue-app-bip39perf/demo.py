#!/usr/bin/env python
#*******************************************************************************
#*   Ledger Blue
#*   (c) 2016 Ledger
#*
#*  Licensed under the Apache License, Version 2.0 (the "License");
#*  you may not use this file except in compliance with the License.
#*  You may obtain a copy of the License at
#*
#*      http://www.apache.org/licenses/LICENSE-2.0
#*
#*  Unless required by applicable law or agreed to in writing, software
#*  distributed under the License is distributed on an "AS IS" BASIS,
#*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#*  See the License for the specific language governing permissions and
#*  limitations under the License.
#********************************************************************************
from ledgerblue.comm import getDongle
import time

mnemonic = raw_input("Enter mnemonic : ")
dongle = getDongle(True)
apdu = bytes("80020000".decode('hex')) + chr(len(mnemonic)) + bytes(mnemonic)
startTime = time.time()
seed = dongle.exchange(apdu)
stopTime = time.time()
print "seed " + str(seed).encode('hex') + " time " + str(stopTime - startTime) + " seconds"
apdu = bytes("80040000".decode('hex')) + chr(len(mnemonic)) + bytes(mnemonic)
startTime = time.time()
seed = dongle.exchange(apdu)
stopTime = time.time()
print "seed " + str(seed).encode('hex') + " time " + str(stopTime - startTime) + " seconds"

