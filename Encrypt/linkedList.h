#pragma once
#include "global.h"

typedef struct EPT_ENCRYPTED_FILE
{
	LIST_ENTRY ListEntry;
	CHAR EncryptedFileName[260];
	INT OrigFileName;

}EPT_ENCRYPTED_FILE, * PEPT_ENCRYPTED_FILE;

LIST_ENTRY EncryptedFileListHead;
KSPIN_LOCK EncryptedFileListSpinLock;
ERESOURCE EncryptedFileListResource;



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

LIST_ENTRY ProcessRulesListHead;
KSPIN_LOCK ProcessRulesListSpinLock;
ERESOURCE ProcessRulesListResource;

NTSTATUS EptIsPRInLinkedList(IN OUT PEPT_PROCESS_RULES ProcessRules);

NTSTATUS EptReplacePRInLinkedList(IN EPT_PROCESS_RULES ProcessRules);

VOID EptListCleanUp();;