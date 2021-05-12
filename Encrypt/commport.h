#pragma once

#include "global.h"

BOOLEAN EptInitCommPort();

VOID EptCloseCommPort();

typedef struct EPT_MESSAGE_HEADER
{
	int Command;
	int Length;
}EPT_MESSAGE_HEADER, * PEPT_MESSAGE_HEADER;

//��չ���� , ��Ӣ�ģ��ָ����� , ��Ӣ�ģ����� ���磺txt,docx������count�м�¼����
typedef struct EPT_PROCESS_RULES
{
	char TargetProcessName[260];
	char TargetExtension[100];
	int count;
}EPT_PROCESS_RULES, * PEPT_PROCESS_RULES;

extern EPT_PROCESS_RULES ProcessRules;