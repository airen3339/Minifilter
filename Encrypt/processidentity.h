#pragma once

#include "global.h"

#define PROCESS_RULES_BUFFER_TAG 'prBT'

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

}EPT_PROCESS_RULES, * PEPT_PROCESS_RULES;

extern LIST_ENTRY ListHead;

VOID EptListCleanUp();

BOOLEAN EptIsTargetProcess(PFLT_CALLBACK_DATA Data);

BOOLEAN EptIsTargetExtension(PFLT_CALLBACK_DATA Data);