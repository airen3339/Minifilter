# Minifilter
参考《Windows内核安全与驱动开发》的加密解密Minifilter

# 运行环境：

Windows 10 x64

Visual Studio 2019

# 更新日志：

2021.04.16 项目立项

2021.05.05 实现了基于SwapBuffers的异或加密解密

2021.05.07 写入、识别加密文件头，对记事本隐藏加密文件头

2021.05.10 实现了应用端到驱动的简单通信

2021.05.12 实现了从客户端传入信任进程和扩展名匹配规则到驱动

2021.05.13 大更新，不再使用《Windows内核安全与驱动开发》对于ByteOffset和EndOfFile的处理方式

2021.05.15 使用微软Cryptography API: Next Generation（CNG）库的AES-128 ECB模式

2021.05.16 完善匹配规则，实现双向链表存储

2021.05.19 驱动中实现进程可执行文件的Hash验证（SHA-256）

2021.05.20 解决BCryptEncrypt自动填充数据，但EOF没有改变，导致文件移动后，加密后的填充数据丢失

# 发展方向：

接下来将会考虑双缓冲方面的问题（double fcb）；

考虑客户端安全性的问题：防止Process Hollowing，线程注入；

# 未修复的bug：

复制粘贴已加密文件，有一定几率会先进入PreWrite，造成重复加密，导致数据损坏

# 参考：
因为书中是基于传统文件过滤驱动的，用在Minifilter中有很多的出入，因此参考了很多相关的资料，谢谢

https://github.com/microsoft/Windows-driver-samples/tree/master/filesys/miniFilter/swapBuffers

https://github.com/microsoft/Windows-classic-samples

https://github.com/minglinchen/WinKernelDev/tree/master/crypt_file

https://github.com/SchineCompton/Antinvader

https://github.com/shines77/Antinvader2015

https://github.com/comor86/MyMiniEncrypt

https://github.com/guidoreina/minivers

https://github.com/xiao70/X70FSD

《Windows内核安全与驱动开发》

《Windows NT File System Internals》

何明 基于Minifilter微框架的文件加解密系统的设计与实现 2014 年 6 月

刘晗 基于双缓冲过滤驱动的透明加密系统研究与实现 2010 年 4 月

# 以下是主要的步骤：

## 按照逐步搭建最小化系统的原则，先根据微软Sample的SwapBuffers实现简单的加密，解密

//首先需要把项目属性Drivers Settings->Target Platform改成Desktop

//关于缓冲方面的问题，加上IRP_MJ_CLEANUP，在PreCleanUp中清一下缓存，EptFileCacheClear(FltObjects->FileObject);

//在PreRead和PreWrite中过滤掉以下两步
```
if (!FlagOn(Data->Iopb->IrpFlags, (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO | IRP_NOCACHE)))

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
```
//判断是否为目标扩展名，进一步筛选，减少后续操作
```
if (!EptIsTargetExtension(Data))

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
```
//然后注意，PostRead中，是在RtlCopyMemory之前解密；PreWrite中，是在RtlCopyMemory之后加密

//但是PreWrite中，iopb->Parameters.Write.Length的大小是0x1000，和真正数据长度是不符的，

//可以使用FltQueryInformationFile查询EOF获得文件的真正大小

//这样基本上就可以了。

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

## 关于写入，识别，对记事本隐藏加密文件头，这部分完全按照《Windows内核安全与驱动开发》是不合适的

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//写入加密文件头"ENCRYPTION"大小PAGE_SIZE

//这里不需要用FltSetInformationFile分配EOF大小

//初始化事件（重要，必须等待FltWriteFile和FltReadFile完成）
```
KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
```
//写入加密标记头
```
ByteOffset.QuadPart = 0;
Status = FltWriteFile(FltObjects->Instance, FltObjects->FileObject, &ByteOffset, Length, Buffer,
	FLTFL_IO_OPERATION_NON_CACHED | FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET, NULL, EptReadWriteCallbackRoutine, &Event);
```
//等待FltWriteFile完成
```
KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);
```
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//读取加密文件头  
//将文件读入缓冲区
```
Length = FILE_FLAG_SIZE;（重要）

KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

ByteOffset.QuadPart = 0;
Status = FltReadFile(FltObjects->Instance, FltObjects->FileObject, &ByteOffset, Length, ReadBuffer,
    	FLTFL_IO_OPERATION_NON_CACHED | FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET, NULL, EptReadWriteCallbackRoutine, &Event);

KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);
```
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//对记事本隐藏文件头

//Read和Write同理，这里只展示Read  
//PreRead:

//忽略以下操作（重要）
```
if (FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_FAST_IO_OPERATION))
{
    return FLT_PREOP_DISALLOW_FASTIO;
}

if (Data->Iopb->Parameters.Read.Length == 0)
{
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

if (!FlagOn(Data->Iopb->IrpFlags, (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO | IRP_NOCACHE)))
{
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
```
//设置偏移加FILE_FLAG_SIZE，Write不需要修改偏移（重要）
```
Data->Iopb->Parameters.Read.ByteOffset.QuadPart += FILE_FLAG_SIZE;
FltSetCallbackDataDirty(Data);
```
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//第三步，这里我们把以上两部分组合在一起，实现一个最小化的基本功能的加密解密系统

//这里需要添加的是IRP_MJ_QUERY_INFORMATION  
//因为之前加上了PAGE_SIZE大小的文件加密头；所以需要在PostQueryInformation中EOF减掉PAGE_SIZE,  
//否则记事本每次保存都会在数据之后加上PAGE_SIZE的空白  
//但是并不需要在PreSetInformation中设置任何的偏移  
//应该是因为KeWaitForSingleObject保证了文件加密头的读写是在Notapad打开之前  
//这样文件加密头的写入就像正常读写一样

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

## 应用端到驱动的简单通信

//这一步按照《Windows内核安全与驱动开发》就可以了  
//使用了如下结构体作为数据头
```
typedef struct EPT_MESSAGE_HEADER
{
	int Command;
	int Length;
}EPT_MESSAGE_HEADER, * PEPT_MESSAGE_HEADER;
```

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

## 从客户端传入信任进程和扩展名匹配规则到驱动

//使用结构体    
//扩展名用 , （英文）分隔，用 , （英文）结束 例如：txt,docx，并在count中记录数量
```
typedef struct EPT_PROCESS_RULES
{
	char TargetProcessName[260];
	char TargetExtension[100];
	int count;
}EPT_PROCESS_RULES, * PEPT_PROCESS_RULES;
```
//客户端发送进程规则
```
memset(Buffer, 0, MESSAGE_SIZE);
MessageHeader.Command = 2;
MessageHeader.Length = MESSAGE_SIZE - sizeof(MessageHeader);
RtlMoveMemory(Buffer, &MessageHeader, sizeof(MessageHeader));

RtlMoveMemory(ProcessRules.TargetProcessName, "notepad.exe", sizeof("notepad.exe"));
RtlMoveMemory(ProcessRules.TargetExtension, "txt,", sizeof("txt,"));
ProcessRules.count = 1;
RtlMoveMemory(Buffer + sizeof(MessageHeader), &ProcessRules, sizeof(EPT_PROCESS_RULES));

if (!EptUserSendMessage(Buffer))
{
	printf("EptUserSendMessage failed.\n");
	return 0;
}
```
//在驱动MessageNotifyCallback函数中接收
```
RtlMoveMemory(&ProcessRules, Buffer + sizeof(EPT_MESSAGE_HEADER), sizeof(EPT_PROCESS_RULES));
```
//将扩展名分隔开，并比较
```
for (int i = 0; i < ProcessRules.count; i++)
    {
        memset(TempExtension, 0, sizeof(TempExtension));
        count = 0;
        while (strncmp(lpExtension, ",", 1))
        {
            TempExtension[count++] = *lpExtension;
            //DbgPrint("lpExtension = %s.\n", lpExtension);
            lpExtension++;
        }

        RtlInitAnsiString(&AnsiTempExtension, TempExtension);
        AnsiTempExtension.MaximumLength = sizeof(TempExtension);

        if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&Extension, &AnsiTempExtension, TRUE)))
        {
            if (RtlEqualUnicodeString(&FileNameInfo->Extension, &Extension, TRUE))
            {
                FltReleaseFileNameInformation(FileNameInfo);
                RtlFreeUnicodeString(&Extension);
                //DbgPrint("EptIsTargetExtension hit.\n");
                return TRUE;
            }

            RtlFreeUnicodeString(&Extension);
        }
        //跳过逗号
        lpExtension++;
    }
```

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

## AES-128 ECB

//需要在微软官网下载cpdksetup.exe，项目->属性的VC++目录的包含目录，库目录设置相应的位置  
//链接器的常规->附加库目录C:\Windows Kits\10\Cryptographic Provider Development Kit\Lib\x64  
//输入->附加依赖项一定要设置为ksecdd.lib  

//按照微软的sample修改，https://docs.microsoft.com/en-us/windows/win32/seccng/encrypting-data-with-cng  
//在DriverEntry中初始化加密的Key，存入全局变量AES_INIT_VARIABLES AesInitVar中，  
//在EncryptUnload中CleanUp相关的Key和分配的内存  
```
typedef struct AES_INIT_VARIABLES
{
    BCRYPT_ALG_HANDLE hAesAlg;
    BCRYPT_KEY_HANDLE hKey;
    PUCHAR pbKeyObject;
    BOOLEAN Flag;
}AES_INIT_VARIABLES, * PAES_INIT_VARIABLES;
```
//但这里我们用的是ECB，所以BCryptEncrypt，BCryptDecrypt函数不需要pbIV，也就是不需要初始块  
//另外将参数设置为BCRYPT_BLOCK_PADDING，这样也不用手动对齐数据，函数会自动填补到AES_BLOCK_SIZE  
//但是还是需要做一个对齐，以便分配在加解密函数中使用的TempBuffer的大小  
```
ULONG OrigLength = EptGetFileSize(FltObjects) - FILE_FLAG_SIZE;
ULONG Length = OrigLength;
if ((Length % AES_BLOCK_SIZE) != 0)
{
    Length = (Length / AES_BLOCK_SIZE + 1) * AES_BLOCK_SIZE;
}
```
//因为每次Data->Iopb->Parameters.Write.Length和Data->Iopb->Parameters.Read.Length都是PAGE_SIZE的整数倍  
//又因为加密之后的数据比原始数据大，所以在PostRead中不需要调用BCryptDecrypt获取解密后数据大小  
//但是PreWrite需要调用BCryptEncrypt返回加密后数据的大小，根据这个大小分配之后替换的内存，  
//之后再调用BCryptEncrypt加密数据  
```
if (ReturnLengthFlag)
{

//BCRYPT_BLOCK_PADDING
//Allows the encryption algorithm to pad the data to the next block size. 
//If this flag is not specified, the size of the plaintext specified in the cbInput parameter 
//must be a multiple of the algorithm's block size.

Status = BCryptEncrypt(AesInitVar.hKey, TempBuffer, OrigLength, NULL, NULL, 0, NULL, 0, LengthReturned, BCRYPT_BLOCK_PADDING);

    if (!NT_SUCCESS(Status))
    {
        DbgPrint("EptAesEncrypt BCryptEncrypt failed.\n");
        ExFreePoolWithTag(TempBuffer, ENCRYPT_TEMP_BUFFER);
        return FALSE;
    }

    DbgPrint("PreWrite AesEncrypt Length = %d LengthReturned = %d.\n", Length, *LengthReturned);

    ExFreePoolWithTag(TempBuffer, ENCRYPT_TEMP_BUFFER);
    return TRUE;
}


Status = BCryptEncrypt(AesInitVar.hKey, TempBuffer, OrigLength, NULL, NULL, 0, Buffer, *LengthReturned, LengthReturned, BCRYPT_BLOCK_PADDING);

if (!NT_SUCCESS(Status))
{
    DbgPrint("EptAesEncrypt BCryptEncrypt failed Status = %X.\n", Status);
    ExFreePoolWithTag(TempBuffer, ENCRYPT_TEMP_BUFFER);
    return FALSE;
}
```

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

## 完善匹配规则，实现双向链表存储

//使用以下结构体
```
typedef struct EPT_PROCESS_RULES
{
	LIST_ENTRY ListEntry;
	char TargetProcessName[260];
	char TargetExtension[100];
	int count;

}EPT_PROCESS_RULES, * PEPT_PROCESS_RULES;
```
//在DriverEntry中InitializeListHead(&ListHead);，在Unload中EptListCleanUp();，释放所有内存
```
VOID EptListCleanUp()
{
PEPT_PROCESS_RULES ProcessRules;
PLIST_ENTRY pListEntry;

while (!IsListEmpty(&ListHead))
{
    pListEntry = RemoveTailList(&ListHead);

    ProcessRules = CONTAINING_RECORD(pListEntry, EPT_PROCESS_RULES, ListEntry);
    DbgPrint("Remove list node TargetProcessName = %s", ProcessRules->TargetProcessName);

    ExFreePool(ProcessRules);
}

}
```
//在驱动MessageNotifyCallback函数中接收并插入链表
```
PEPT_PROCESS_RULES ProcessRules;
ProcessRules = ExAllocatePoolWithTag(PagedPool, sizeof(EPT_PROCESS_RULES), PROCESS_RULES_BUFFER_TAG);
if (!ProcessRules)
{
    DbgPrint("DriverEntry ExAllocatePoolWithTag ProcessRules failed.\n");
    return 0;
}

RtlZeroMemory(ProcessRules, sizeof(EPT_PROCESS_RULES));

RtlMoveMemory(ProcessRules->TargetProcessName, Buffer + sizeof(EPT_MESSAGE_HEADER), sizeof(EPT_PROCESS_RULES) - sizeof(LIST_ENTRY));

//DbgPrint("InsertTailList ProcessRules = %s ProcessRules->TargetProcessName = %s.\n", ProcessRules, ProcessRules->TargetProcessName);
InsertTailList(&ListHead, &ProcessRules->ListEntry);

break;
```
//使用以下方式遍历比较进程名和扩展名
```
PEPT_PROCESS_RULES ProcessRules;
PLIST_ENTRY pListEntry = ListHead.Flink;

while (pListEntry != &ListHead)
{

    ProcessRules = CONTAINING_RECORD(pListEntry, EPT_PROCESS_RULES, ListEntry);

    //比较操作
		
    pListEntry = pListEntry->Flink;

}
```

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

## 驱动中实现进程可执行文件的Hash验证
```
//这里用到了Windows-classic-samples-master\Samples\Security\SignHashAndVerifySignature
//中的ComputeHash函数计算Hash
NTSTATUS ComputeHash(
	PUCHAR Data, 
	ULONG DataLength, 
	PUCHAR* DataDigestPointer, 
	ULONG* DataDigestLengthPointer)
```
//把exe文件读到Buffer
```
NTSTATUS EptReadProcessFile(
	UNICODE_STRING ProcessName,
	PUCHAR* Buffer,
	PULONG Length
	)
{
	OBJECT_ATTRIBUTES ObjectAttributes;
	NTSTATUS Status;
	HANDLE FileHandle;
	IO_STATUS_BLOCK IoStatusBlock;

	FILE_STANDARD_INFORMATION FileStandInfo;
	LARGE_INTEGER ByteOffset;

	InitializeObjectAttributes(
		&ObjectAttributes, 
		&ProcessName, 
		OBJ_CASE_INSENSITIVE, 
		NULL, 
		NULL);

	Status = ZwOpenFile(
		&FileHandle, 
		GENERIC_READ,
		&ObjectAttributes, 
		&IoStatusBlock, 
		FILE_SHARE_VALID_FLAGS,
		FILE_NON_DIRECTORY_FILE);

	if (!NT_SUCCESS(Status))
	{
		//STATUS_SHARING_VIOLATION
		DbgPrint("EptReadProcessFile ZwOpenFile failed Status = %X.\n", Status);
		return Status;
	}

	//查询文件大小，分配内存
	Status = ZwQueryInformationFile(
		FileHandle, 
		&IoStatusBlock, 
		&FileStandInfo, 
		sizeof(FILE_STANDARD_INFORMATION), 
		FileStandardInformation);

	if (!NT_SUCCESS(Status))
	{
		DbgPrint("EptReadProcessFile ZwQueryInformationFile failed.\n");
		ZwClose(FileHandle);
		return Status;
	}

	(*Buffer) = ExAllocatePoolWithTag(
		PagedPool, 
		FileStandInfo.EndOfFile.QuadPart, 
		PROCESS_FILE_BUFFER_TAG);

	if (!(*Buffer))
	{
		DbgPrint("EptReadProcessFile ExAllocatePoolWithTag Buffer failed.\n");
		ZwClose(FileHandle);
		return Status;
	}

	ByteOffset.QuadPart = 0;
	Status = ZwReadFile(
		FileHandle, 
		NULL, NULL, NULL, 
		&IoStatusBlock, 
		(*Buffer),
		(ULONG)FileStandInfo.EndOfFile.QuadPart, 
		&ByteOffset, 
		NULL);

	if (!NT_SUCCESS(Status))
	{
		DbgPrint("EptReadProcessFile ZwReadFile failed.\n");
		ZwClose(FileHandle);
		ExFreePool((*Buffer));
		return Status;
	}

	*Length = (ULONG)FileStandInfo.EndOfFile.QuadPart;
	return Status;
}
```
//在EptIsTargetProcess函数中判断Hash，CheckHash标志位是全局变量，在PreCreate中时，设为TRUE
```
if(CheckHash)
{
    PUCHAR ReadBuffer = NULL;
    ULONG Length;
    Status = EptReadProcessFile(*ProcessName, &ReadBuffer, &Length);

    if (NT_SUCCESS(Status))
    {
					
        if (EptVerifyHash(ReadBuffer, Length, ProcessRules->Hash))
        {
            if (ReadBuffer)
                ExFreePool(ReadBuffer);
            CheckHash = FALSE;
            return TRUE;
        }
        else
        {
            if (ReadBuffer)
                ExFreePool(ReadBuffer);
            CheckHash = FALSE;
            return FALSE;
        }
    }
    return FALSE;
}
```
//这里在从客户端传入Hash到驱动之前，对Hash进行转换  
//因为ULONGLONG是小端序，  
//而ComputeHash输出的是十六进制的Hash值，是大端序  
//做一下转换
```
ULONGLONG Hash[4];
Hash[0] = 0xa28438e1388f272a;
Hash[1] = 0x52559536d99d65ba;
Hash[2] = 0x15b1a8288be1200e;
Hash[3] = 0x249851fdf7ee6c7e;

ULONGLONG TempHash;
RtlZeroMemory(ProcessRules->Hash, sizeof(ProcessRules->Hash));
    
for (ULONG i = 0; i < 4; i++)
{
    TempHash = Hash[i];
    for (ULONG j = 0; j < 8; j++)
    {
        ProcessRules->Hash[8 * (i + 1) - 1 - j] = TempHash % 256;
        TempHash = TempHash / 256;
    }
}
```

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

## 解决BCryptEncrypt自动填充数据，但EOF没有改变，导致文件移动后，加密后的填充数据丢失
//在PreSetInformation中，将EOF对齐AES_BLOCK_SIZE
```
PVOID InfoBuffer = Data->Iopb->Parameters.SetFileInformation.InfoBuffer;

    switch (Data->Iopb->Parameters.QueryFileInformation.FileInformationClass)
    {

    case FileEndOfFileInformation:
    {
        PFILE_END_OF_FILE_INFORMATION Info = (PFILE_END_OF_FILE_INFORMATION)InfoBuffer;
        if (Info->EndOfFile.QuadPart % AES_BLOCK_SIZE != 0)
        {
            Info->EndOfFile.QuadPart = (Info->EndOfFile.QuadPart / AES_BLOCK_SIZE + 1) * AES_BLOCK_SIZE;
        }
        DbgPrint("EndOfFile = %d.\n", Info->EndOfFile.QuadPart);
        break;
    }

    }
```
//在PostQueryInformation中，将EOF对齐AES_BLOCK_SIZE
```
case FileEndOfFileInformation:
    {
        //DbgPrint("5.\n");
        PFILE_END_OF_FILE_INFORMATION Info = (PFILE_END_OF_FILE_INFORMATION)InfoBuffer;
        Info->EndOfFile.QuadPart -= FILE_FLAG_SIZE;
        if (Info->EndOfFile.QuadPart % AES_BLOCK_SIZE != 0)
        {
            Info->EndOfFile.QuadPart = (Info->EndOfFile.QuadPart / AES_BLOCK_SIZE + 1) * AES_BLOCK_SIZE;
        }
        break;
    }
```
//在EptAesDecrypt(PUCHAR Buffer, ULONG Length)中将Length += AES_BLOCK_SIZE;  
//这一步是因为BCryptEncrypt即便是已经对齐，仍然会自动填充AES_BLOCK_SIZE大小的数据  
//所以，PostRead中，长度应该再加AES_BLOCK_SIZE  
//这样每次移动，仍然会丢失AES_BLOCK_SIZE大小的数据，但是因为AES-128 ECB加密前后的数据是对应的  
//而丢失的这块数据是已经对齐后，仍然会自动填充AES_BLOCK_SIZE大小的数据，所以并不影响正常数据的解密
