void KMS_Reset(void);

void KMS_Ctrl_Snd_Write(void);
void KMS_Stat_Snd_Read(void);
void KMS_Ctrl_KM_Write(void);
void KMS_Stat_KM_Read(void);
void KMS_Ctrl_TX_Write(void);
void KMS_Stat_TX_Read(void);
void KMS_Ctrl_Cmd_Write(void);
void KMS_Stat_Cmd_Read(void);

void KMS_Data_Write(void);
void KMS_Data_Read(void);

void KMS_KM_Data_Read(void);

void kms_keydown(Uint8 modkeys, Uint8 keycode);
void kms_keyup(Uint8 modkeys, Uint8 keycode);
void kms_mouse_move(int x, bool left, int y, bool up);
void kms_mouse_button(bool left, bool down);

bool kms_send_codec_receive(Uint32 data);
bool kms_can_receive_codec(void);
void kms_send_sndout_underrun(void);
void kms_send_sndout_request(void);

void Mouse_Handler(void);
