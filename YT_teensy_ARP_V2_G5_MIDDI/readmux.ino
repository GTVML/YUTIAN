void readmux(){
s0 = digitalRead(48);
s1 = digitalRead(51);
s2 = digitalRead(52);
s3 = digitalRead(53);
s4 = digitalRead(54);
s5 = digitalRead(55);
s6 = digitalRead(56);
s7 = digitalRead(57);

check0 = analogRead(A17);
if(abs(precheck0-check0)>20){
  precheck0=check0;
  r0 = precheck0;
}
check1 = analogRead(A18);
if(abs(precheck1-check1)>20){
  precheck1=check1;
  r1 = precheck1;
}
check2 = analogRead(A19);
if(abs(precheck2-check2)>20){
  precheck2=check2;
  r2 = precheck2;
}
check3 = analogRead(A20);
if(abs(precheck3-check3)>20){
  precheck3=check3;
  r3 = precheck3;
}
check4 = analogRead(A21);
if(abs(precheck4-check4)>20){
  precheck4=check4;
  r4 = precheck4;
}
check5 = analogRead(A22);
if(abs(precheck5-check5)>20){
  precheck5=check5;
  r5 = precheck5;
}
check6 = analogRead(A23);
if(abs(precheck6-check6)>20){
  precheck6=check6;
  r6 = precheck6;
}
check7 = analogRead(A24);
if(abs(precheck7-check7)>20){
  precheck7=check7;
  r7 = precheck7;
}
}
