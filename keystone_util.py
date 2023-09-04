#!/usr/bin/python
# -*- coding: UTF-8 -*-
import base64,uuid
import time
import os
import binascii
	

if __name__ == "__main__":
    expires_at_timestrap = time.time() # 对应请求头 p-time-stamp eg
    audit_id = base64.urlsafe_b64encode(uuid.uuid4().bytes)[:-2].decode('utf-8')
    # 对应请求头 fingerprint eg p_Yj_In3TAy_5pjZVDNRtA
    #IV = os.urandom(16) # 对应请求头 fingerprint2 eg 'ty\xfc\xe8\x8a\xd3?
    IV = binascii.hexlify(os.urandom(16))
    print "{}:::{}:::{}".format(expires_at_timestrap, audit_id, IV)
    #print binascii.hexlify(os.urandom(16))
