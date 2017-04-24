// Low level camera commands ----------------------

void cam_sendCommand(uint8_t cmd, uint8_t args[] = 0, uint8_t argn = 0) {
  Serial.write((byte)0x56);
  Serial.write((byte)serialNum);
  Serial.write((byte)cmd);

  for (uint8_t i = 0; i < argn; i++) {
    Serial.write((byte)args[i]);
  }
}

uint8_t cam_readResponse(uint8_t numbytes, uint8_t timeout) {
  uint8_t counter = 0;
  bufferLen = 0;
  int avail;

  while ((timeout != counter) && (bufferLen != numbytes)) {
    avail = Serial.available();
    if (avail <= 0) {
      delay(1);
      counter++;
      continue;
    }
    counter = 0;
    // there's a byte!
    camerabuff[bufferLen++] = Serial.read();
  }

  return bufferLen;
}

boolean cam_verifyResponse(uint8_t command) {
  if ((camerabuff[0] != 0x76) ||
      (camerabuff[1] != serialNum) ||
      (camerabuff[2] != command) ||
      (camerabuff[3] != 0x0))
    return false;
  return true;

}

boolean cam_runCommand(uint8_t cmd, uint8_t *args, uint8_t argn, uint8_t resplen, boolean flushflag = true) {
  // flush out anything in the buffer?
  if (flushflag) {
    cam_readResponse(100, 10);
  }

  cam_sendCommand(cmd, args, argn);
  if (cam_readResponse(resplen, 200) != resplen)
    return false;
  if (! cam_verifyResponse(cmd))
    return false;
  return true;
}

// Camera control ---------------------------------

boolean cam_reset() {
  uint8_t args[] = {0x0};
  return cam_runCommand(VC0706_RESET, args, 1, 5);
}

char * cam_getVersion(void) {
  uint8_t args[] = {0x01};

  cam_sendCommand(VC0706_GEN_VERSION, args, 1);
  // get reply
  if (!cam_readResponse(CAMERABUFFSIZ, 200))
    return 0;
  camerabuff[bufferLen] = 0;  // end it!
  return (char *)camerabuff;  // return it!
}

boolean cam_setImageSize(uint8_t x) {
  uint8_t args[] = {0x05, 0x04, 0x01, 0x00, 0x19, x};

  return cam_runCommand(VC0706_WRITE_DATA, args, sizeof(args), 5);
}

uint8_t cam_getImageSize() {
  uint8_t args[] = {0x4, 0x4, 0x1, 0x00, 0x19};
  if (! cam_runCommand(VC0706_READ_DATA, args, sizeof(args), 6))
    return -1;

  return camerabuff[5];
}

boolean cam_frameBuffCtrl(uint8_t command) {
  uint8_t args[] = {0x1, command};
  return cam_runCommand(VC0706_FBUF_CTRL, args, sizeof(args), 5);
}

boolean cam_takePicture() {
  frameptr = 0;
  return cam_frameBuffCtrl(VC0706_STOPCURRENTFRAME);
}

uint32_t cam_frameLength(void) {
  uint8_t args[] = {0x01, 0x00};
  if (!cam_runCommand(VC0706_GET_FBUF_LEN, args, sizeof(args), 9))
    return 0;

  uint32_t len;
  len = camerabuff[5];
  len <<= 8;
  len |= camerabuff[6];
  len <<= 8;
  len |= camerabuff[7];
  len <<= 8;
  len |= camerabuff[8];

  return len;
}

uint8_t * cam_readPicture(uint8_t n) {
  uint8_t args[] = {0x0C, 0x0, 0x0A, 
                    0, 0, frameptr >> 8, frameptr & 0xFF, 
                    0, 0, 0, n, 
                    CAMERADELAY >> 8, CAMERADELAY & 0xFF};

  if (! cam_runCommand(VC0706_READ_FBUF, args, sizeof(args), 5, false))
    return 0;


  // read into the buffer PACKETLEN!
  if (cam_readResponse(n+5, CAMERADELAY) == 0) 
      return 0;


  frameptr += n;

  return camerabuff;
}
