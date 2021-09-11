#pragma once

#include "global.h"

#define PROCESS_RULES_BUFFER_TAG 'prBT'
#define PROCESS_RULES_HASH_TAG 'prHT'
#define PROCESS_FILE_BUFFER_TAG 'pfBT'

typedef NTSTATUS(*EptQueryInformationProcess) (
	_In_      HANDLE,
	_In_      PROCESSINFOCLASS,
	_Out_     PVOID,
	_In_      ULONG,
	_Out_opt_ PULONG
	);

extern EptQueryInformationProcess pEptQueryInformationProcess;

//��չ���� , ��Ӣ�ģ��ָ����� , ��Ӣ�ģ����� ���磺txt,docx������count�м�¼����
typedef struct EPT_PROCESS_RULES
{
	LIST_ENTRY ListEntry;
	char TargetProcessName[260];
	char TargetExtension[100];
	int count;
	UCHAR Hash[32];
	BOOLEAN IsCheckHash;

}EPT_PROCESS_RULES, * PEPT_PROCESS_RULES;

extern LIST_ENTRY ListHead;
extern KSPIN_LOCK List_Spin_Lock;


VOID EptListCleanUp();

NTSTATUS ComputeHash(IN PUCHAR Data, IN ULONG DataLength, IN OUT PUCHAR* DataDigestPointer, IN OUT ULONG* DataDigestLengthPointer);

NTSTATUS EptIsTargetProcess(IN PFLT_CALLBACK_DATA Data);

BOOLEAN EptIsTargetExtension(IN PFLT_CALLBACK_DATA Data);