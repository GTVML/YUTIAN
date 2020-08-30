void Sync() {
  waveform1.frequency(scale[i]);
  MIDI.sendNoteOn(midiscale[i], 90, 1); 
  Oct();
  Scale();
  tun();
  beat = beat +1;
 if(beat > 7){beat = 0;}

// step 1
if(beat == 0){
  if(s0 == 1){envelope1.noteOn();
 }else{ waveform1.frequency(0);}
 if(digitalRead(noterandom) ==HIGH){
  note = r0;
  note = map(note,0,1023,0,7);

  }else if(digitalRead(noterandom)==LOW){
  note = random(0,8);
 }
 i = r[note]+tune+Octave[octnum];
 ledstep7();
 
 }

//step 2
 if(beat == 1){
  if(s1 == 1){envelope1.noteOn();
 }else{waveform1.frequency(0);}
  if(digitalRead(noterandom) ==HIGH){
  note = r1;
  note = map(note,0,1023,0,7);
  }else if(digitalRead(noterandom)==LOW){
  note = random(0,8);
  }
  i = r[note]+tune+Octave[octnum];
 ledstep0();
  }

  
//step 3
if(beat == 2){
  if(s2 == 1){envelope1.noteOn();
 }else{waveform1.frequency(0);}
 if(digitalRead(noterandom) ==HIGH){
  note = r2;
  note = map(note,0,1023,0,7);
  }else if(digitalRead(noterandom)==LOW){
  note = random(0,8);
 }
 i = r[note]+tune+Octave[octnum];
  ledstep1();
 }

//step 4
 if(beat == 3){
  if(s3 == 1){
    envelope1.noteOn();
 }else{waveform1.frequency(0);}
 if(digitalRead(noterandom) ==HIGH){
  note = r3;
  note = map(note,0,1023,0,7);
  }else if(digitalRead(noterandom)==LOW){
  note = random(0,8);
 }
 i = r[note]+tune+Octave[octnum];
  ledstep2();
 }

 
//step5
 if(beat == 4){
  if(s4 == 1){envelope1.noteOn();
 }else{waveform1.frequency(0);}
 if(digitalRead(noterandom) ==HIGH){
  note = r4;
  note = map(note,0,1023,0,7);
  }else if(digitalRead(noterandom)==LOW){
  note = random(0,8);
 }
 i = r[note]+tune+Octave[octnum];
  ledstep3();
 }

 //step6
 if(beat == 5){
  if(s5 == 1){envelope1.noteOn();
 }else{waveform1.frequency(0);}
 if(digitalRead(noterandom) ==HIGH){
  note = r5;
  note = map(note,0,1023,0,7);
  }else if(digitalRead(noterandom)==LOW){
  note = random(0,8);
 }
 i = r[note]+tune+Octave[octnum];
  ledstep4();
 }

  //step7
 if(beat == 6){
  if(s6 == 1){envelope1.noteOn();
 }else{waveform1.frequency(0);}
 if(digitalRead(noterandom) ==HIGH){
  note = r6;
  note = map(note,0,1023,0,7);
  }else if(digitalRead(noterandom)==LOW){
  note = random(0,8);
 }
 i = r[note]+tune+Octave[octnum];
  ledstep5();
 }

  //step7
 if(beat == 7){
  if(s7 == 1){envelope1.noteOn();
 }else{waveform1.frequency(0);}
 if(digitalRead(noterandom) ==HIGH){
  note = r7;
  note = map(note,0,1023,0,7);
  }else if(digitalRead(noterandom)==LOW){
  note = random(0,8);
 }
 i = r[note]+tune+Octave[octnum];
  ledstep6();
 }

}
