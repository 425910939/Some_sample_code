/*This is tone file*/

const char* tone_uri[] = {
   "flsh:///0_Bt_Reconnect.mp3",
   "flsh:///1_Bt_Success.mp3",
   "flsh:///2_New_Version_Available.mp3",
   "flsh:///3_Out_Of_Power.mp3",
   "flsh:///4_Please_Retry_Wifi.mp3",
   "flsh:///5_Under_Smartconfig.mp3",
   "flsh:///6_Upgrade_Done.mp3",
   "flsh:///7_Welcome_To_Bt.mp3",
   "flsh:///8_Welcome_To_Wifi.mp3",
   "flsh:///9_Wifi_Reconnect.mp3",
   "flsh:///10_Wifi_Success.mp3",
   "flsh:///11_Wifi_Time_Out.mp3",
};

int getToneUriMaxIndex()
{
    return sizeof(tone_uri) / sizeof(char*) - 1;
}
                