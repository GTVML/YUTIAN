
void Scale(){
     if(analogRead(scalepin) > 1010 ){arr_number = 10;}
     if(analogRead(scalepin) <= 1010  && analogRead(scalepin) > 1005 ){ arr_number = 9;}
     if(analogRead(scalepin) <= 1005  && analogRead(scalepin) > 900 ){arr_number = 8;}
     if(analogRead(scalepin) <= 900 && analogRead(scalepin) > 780){arr_number = 7;}
     if(analogRead(scalepin) <= 780 && analogRead(scalepin) > 6300){arr_number = 6;}
     if(analogRead(scalepin) <= 630 && analogRead(scalepin) > 440){arr_number = 5;}
     if(analogRead(scalepin) <= 440 && analogRead(scalepin) > 310){arr_number = 4;}
     if(analogRead(scalepin) <= 310 && analogRead(scalepin) > 175){arr_number = 3;}
     if(analogRead(scalepin) <= 175 && analogRead(scalepin) > 70){arr_number = 2;}
     if(analogRead(scalepin) <= 70 && analogRead(scalepin) > 10){arr_number = 1;}
     if(analogRead(scalepin) <= 10 ){arr_number =1;}
      r = arr_list[arr_number];//音阶序列

}
