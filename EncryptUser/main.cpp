
#include "global.h"

HANDLE hPort;

BOOLEAN EptUserInitCommPort()
{
	HRESULT hResult;

	hResult = FilterConnectCommunicationPort(COMMPORTNAME, NULL, NULL, NULL, NULL, &hPort);

	if (hResult != S_OK)
	{
		return FALSE;
	}

	return TRUE;
}


BOOLEAN EptUserSendMessage(IN LPVOID lpInBuffer) 
{

	HRESULT hResult;
	DWORD BytesReturned;

	hResult = FilterSendMessage(hPort, lpInBuffer, sizeof(lpInBuffer), NULL, NULL, &BytesReturned);

	if (hResult != S_OK)
	{
		return FALSE;
	}

	return TRUE;
}


int main() 
{

	EPT_MESSAGE_HEADER MessageHeader;
	EPT_PROCESS_RULES ProcessRules;
	char Buffer[MESSAGE_SIZE];

	printf("Hello World.\n");

	if (!EptUserInitCommPort())
	{
		printf("EptUserInitCommPort failed.\n");
		return 0;
	}

	//����һ��Hello
	memset(Buffer, 0, MESSAGE_SIZE);
	MessageHeader.Command = 1;
	MessageHeader.Length = MESSAGE_SIZE - sizeof(MessageHeader);

	RtlMoveMemory(Buffer, &MessageHeader, sizeof(MessageHeader));
	RtlMoveMemory(Buffer + sizeof(MessageHeader), "Hello driver, test from Encrypt User", sizeof("Hello driver, test from Encrypt User"));

	if (!EptUserSendMessage(Buffer))
	{
		printf("EptUserSendMessage failed.\n");
		return 0;
	}

	//���ͽ��̹���
	memset(Buffer, 0, MESSAGE_SIZE);
	MessageHeader.Command = 2;
	MessageHeader.Length = MESSAGE_SIZE - sizeof(MessageHeader);
	RtlMoveMemory(Buffer, &MessageHeader, sizeof(MessageHeader));

	RtlMoveMemory(ProcessRules.TargetProcessName, "notepad.exe", sizeof("notepad.exe"));
	RtlMoveMemory(ProcessRules.TargetExtension, "txt,c,", sizeof("txt,c,"));
	ProcessRules.count = 2;

	ULONGLONG Hash[4];
	Hash[0] = 0xa28438e1388f272a;
	Hash[1] = 0x52559536d99d65ba;
	Hash[2] = 0x15b1a8288be1200e;
	Hash[3] = 0x249851fdf7ee6c7e;

	ULONGLONG TempHash;
	RtlZeroMemory(ProcessRules.Hash, sizeof(ProcessRules.Hash));

	for (ULONG i = 0; i < 4; i++)
	{
		TempHash = Hash[i];
		for (ULONG j = 0; j < 8; j++)
		{
			ProcessRules.Hash[8 * (i + 1) - 1 - j] = TempHash % 256;
			TempHash = TempHash / 256;
		}

	}

	RtlMoveMemory(Buffer + sizeof(MessageHeader), &ProcessRules, sizeof(EPT_PROCESS_RULES));

	if (!EptUserSendMessage(Buffer))
	{
		printf("[EptUserSendMessage]->failed.\n");
		return 0;
	}


	return 0;
}