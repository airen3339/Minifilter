#pragma warning(disable:4996)

#include "filefunc.h"
#include "commport.h"

ULONG BreakPointFlag = 1;

#define FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK  1


//�Զ���ϵ㣬���Σ���ȡϵͳʱ�䣬�����ȴ�1�룬�ϵ�
VOID EptBreakPointOnce() {

	LARGE_INTEGER SystemTime;
	LARGE_INTEGER LocalTime;
	TIME_FIELDS  TimeFiled;
	KEVENT Event;
	ULONG MircoSecond = 1000000;

	if (BreakPointFlag) {

		BreakPointFlag--;

		//��ȡ��ǰʱ��
		KeQuerySystemTime(&SystemTime);
		ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
		RtlTimeToTimeFields(&LocalTime, &TimeFiled);

		DbgPrint("\n-----------------------------------------------------\n\n");
		DbgPrint("Ept DbgBreakPoint once. Test time = %d-%02d-%02d %02d:%02d.\n"
			, TimeFiled.Year
			, TimeFiled.Month
			, TimeFiled.Day
			, TimeFiled.Hour
			, TimeFiled.Minute);
		DbgPrint("\n-----------------------------------------------------\n");

		//��ʼ��һ���¼��ں˶���, ����ʼ��Ϊ�Ǵ���̬
		KeInitializeEvent(&Event, NotificationEvent, FALSE);
		//���õȴ�ʱ��
		LARGE_INTEGER TimeOut = RtlConvertUlongToLargeInteger(-10 * MircoSecond);
		//�ȴ��¼�����1��
		KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, &TimeOut);

		DbgBreakPoint();

		return;

	}
}


VOID EptReadWriteCallbackRoutine(
    PFLT_CALLBACK_DATA CallbackData,
    PFLT_CONTEXT Context
)
{
    UNREFERENCED_PARAMETER(CallbackData);
    KeSetEvent((PRKEVENT)Context, IO_NO_INCREMENT, FALSE);
}


ULONG EptGetFileSize(PCFLT_RELATED_OBJECTS FltObjects)
{
    FILE_STANDARD_INFORMATION StandardInfo;
    ULONG LengthReturned;

    FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject, &StandardInfo, sizeof(FILE_STANDARD_INFORMATION), FileStandardInformation, &LengthReturned);

    return (ULONG)StandardInfo.EndOfFile.QuadPart;
}


//�ж��Ƿ�Ϊ���м��ܱ�ǵ��ļ�
BOOLEAN EptIsTargetFile(PCFLT_RELATED_OBJECTS FltObjects) {

	NTSTATUS Status;
	PFLT_VOLUME Volume;
	FLT_VOLUME_PROPERTIES VolumeProps;

    KEVENT Event;

	PVOID ReadBuffer;
	LARGE_INTEGER ByteOffset = { 0 };
	ULONG Length;


	//����FltReadFile����Length��Ҫ��Length������������С��������
	Status = FltGetVolumeFromInstance(FltObjects->Instance, &Volume);

	if (!NT_SUCCESS(Status)) {

		DbgPrint("EptIsTargetFile FltGetVolumeFromInstance failed.\n");
		return FALSE;
	}

	Status = FltGetVolumeProperties(Volume, &VolumeProps, sizeof(VolumeProps), &Length);

	/*if (NT_ERROR(Status)) {

		FltObjectDereference(Volume);
		DbgPrint("DEptIsTargetFile FltGetVolumeProperties failed.\n");
		return FALSE;
	}*/

	//DbgPrint("VolumeProps.SectorSize = %d.\n", VolumeProps.SectorSize);

	Length = FILE_FLAG_SIZE;
	Length = ROUND_TO_SIZE(Length, VolumeProps.SectorSize);

	//ΪFltReadFile�����ڴ棬֮����Buffer�в���Flag
	ReadBuffer = FltAllocatePoolAlignedWithTag(FltObjects->Instance, NonPagedPool, Length, 'itRB');

	if (!ReadBuffer) {

		FltObjectDereference(Volume);
		DbgPrint("EptIsTargetFile ExAllocatePool failed.\n");
		return FALSE;
	}

	RtlZeroMemory(ReadBuffer, Length);

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    //���ļ����뻺����
    ByteOffset.QuadPart = 0;
    Status = FltReadFile(FltObjects->Instance, FltObjects->FileObject, &ByteOffset, Length, ReadBuffer,
        FLTFL_IO_OPERATION_NON_CACHED | FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET, NULL, EptReadWriteCallbackRoutine, &Event);

    KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);

   
	if (!NT_SUCCESS(Status)) {

        //STATUS_PENDING
		DbgPrint("EptIsTargetFile FltReadFile failed. Status = %X.\n", Status);
		FltObjectDereference(Volume);
		FltFreePoolAlignedWithTag(FltObjects->Instance, ReadBuffer, 'itRB');
		return FALSE;

	}

	//DbgPrint("EptIsTargetFile Buffer = %p file content = %s.\n", ReadBuffer, (CHAR*)ReadBuffer);

	if (strncmp(FILE_FLAG, ReadBuffer, strlen(FILE_FLAG)) == 0) {

		FltObjectDereference(Volume);
		FltFreePoolAlignedWithTag(FltObjects->Instance, ReadBuffer, 'itRB');
		DbgPrint("EptIsTargetFile hit. TargetFile is match.\n");
		return TRUE;
	}

	FltObjectDereference(Volume);
	FltFreePoolAlignedWithTag(FltObjects->Instance, ReadBuffer, 'itRB');
	return FALSE;
}


//������½����ļ�������д�����ݵ�����д����ܱ��ͷ
BOOLEAN EptWriteFileHeader(PFLT_CALLBACK_DATA* Data, PCFLT_RELATED_OBJECTS FltObjects) {

	NTSTATUS Status;
	FILE_STANDARD_INFORMATION StandardInfo;
	//FILE_END_OF_FILE_INFORMATION FileEOFInfo;

	PFLT_VOLUME Volume;
	FLT_VOLUME_PROPERTIES VolumeProps;

    KEVENT Event;

	PVOID Buffer;
	LARGE_INTEGER ByteOffset;
	ULONG Length, LengthReturned;

	//��ѯ�ļ���С
	Status = FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject, &StandardInfo, sizeof(FILE_STANDARD_INFORMATION), FileStandardInformation, &LengthReturned);

	if (!NT_SUCCESS(Status) || Status == STATUS_VOLUME_DISMOUNTED) {

		//DbgPrint("EptWriteFileHeader FltQueryInformationFile failed.\n");
		return FALSE;
	}

	//DbgPrint("(*Data)->Iopb->Parameters.Create.SecurityContext->DesiredAccess = %x.\n", (*Data)->Iopb->Parameters.Create.SecurityContext->DesiredAccess);

	//�����ļ�ͷFILE_FLAG_SIZE��С��д���ļ�flag
	if (StandardInfo.EndOfFile.QuadPart == 0
		&& ((*Data)->Iopb->Parameters.Create.SecurityContext->DesiredAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA))) {

		if (!NT_SUCCESS(Status)) {

			DbgPrint("EptWriteFileHeader FltSetInformationFile failed.\n");
			return FALSE;
		}
		
		//����FltWriteFile����Length��Ҫ��Length������������С��������
		Status = FltGetVolumeFromInstance(FltObjects->Instance, &Volume);

		if (!NT_SUCCESS(Status)) {

			DbgPrint("EptWriteFileHeader FltGetVolumeFromInstance failed.\n");
			return FALSE;
		}

		Status = FltGetVolumeProperties(Volume, &VolumeProps, sizeof(VolumeProps), &Length);

		Length = max(sizeof(FILE_FLAG), FILE_FLAG_SIZE);
		Length = ROUND_TO_SIZE(Length, VolumeProps.SectorSize);

		FltObjectDereference(Volume);

		Buffer = ExAllocatePoolWithTag(NonPagedPool, Length, 'wiBF');
		if (!Buffer) {

			DbgPrint("EptWriteFileHeader ExAllocatePoolWithTag failed.\n");
			return FALSE;
		}

		RtlZeroMemory(Buffer, Length);

		if (Length >= sizeof(FILE_FLAG))
			RtlMoveMemory(Buffer, FILE_FLAG, sizeof(FILE_FLAG));


        KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

		//д����ܱ��ͷ
		ByteOffset.QuadPart = 0;
		Status = FltWriteFile(FltObjects->Instance, FltObjects->FileObject, &ByteOffset, Length, Buffer,
			FLTFL_IO_OPERATION_NON_CACHED | FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET, NULL, EptReadWriteCallbackRoutine, &Event);

        KeWaitForSingleObject(&Event, Executive, KernelMode, TRUE, 0);

        DbgPrint("EptWriteFileHeader hit.\n");

		if (!NT_SUCCESS(Status)) {

			DbgPrint("EptWriteFileHeader FltWriteFile failed.\n");
			ExFreePool(Buffer);
			return FALSE;
		}

        ExFreePool(Buffer);

		return TRUE;
	}

	return FALSE;
}


//����ļ����壬https://github.com/SchineCompton/Antinvader
VOID EptFileCacheClear(PFILE_OBJECT pFileObject)
{
    // FCB
    PFSRTL_COMMON_FCB_HEADER pFcb;

    // ˯��ʱ�� ����KeDelayExecutionThread
    LARGE_INTEGER liInterval;

    // �Ƿ���Ҫ�ͷ���Դ
    BOOLEAN bNeedReleaseResource = FALSE;

    // �Ƿ���Ҫ�ͷŷ�ҳ��Դ
    BOOLEAN bNeedReleasePagingIoResource = FALSE;

    // IRQL
    KIRQL irql;

    // ѭ��ʱ�Ƿ�����
    BOOLEAN bBreak = TRUE;

    // �Ƿ���Դ������
    BOOLEAN bLockedResource = FALSE;

    // �Ƿ��Ƿ�ҳ��Դ������
    BOOLEAN bLockedPagingIoResource = FALSE;

    // Resource �� PagingIoResource ��Դ�������Ⱥ�˳��
    BOOLEAN isPagingIoResourceLockedFirst = FALSE;

    //
    // ��ȡFCB
    //
    pFcb = (PFSRTL_COMMON_FCB_HEADER)pFileObject->FsContext;

    //
    // ���û��FCB ֱ�ӷ���
    //
    if (pFcb == NULL) {
        /*
        #ifdef DBG
                __asm int 3
        #endif*/
        return;
    }

    //
    // ��֤��ǰIRQL <= APC_LEVEL
    //

    irql = KeGetCurrentIrql();

    if (irql > APC_LEVEL) {
#if defined(DBG) && !defined(_WIN64)
        __asm int 3
#endif
        return;
    }

    //
    // ����˯��ʱ��
    //
    liInterval.QuadPart = -1 * (LONGLONG)50;

    //
    // �����ļ�ϵͳ
    //
    FsRtlEnterFileSystem();

    isPagingIoResourceLockedFirst = FALSE;

    //
    // FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK, ע��: �ú궨���� AntinvaderDef.h ͷ�ļ���
    //
#if defined(FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK) && (FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK != 0)
    //
    // ѭ������, һ��Ҫ����, �������������.
    //
    for (;;) {
        //
        // ��ʼ������
        //
        bBreak = TRUE;
        bNeedReleaseResource = FALSE;
        bNeedReleasePagingIoResource = FALSE;
        bLockedResource = FALSE;
        bLockedPagingIoResource = FALSE;

        //
        // ��FCB������
        //
        if (pFcb->PagingIoResource) {
            if (bLockedPagingIoResource == FALSE) {
                bLockedPagingIoResource = ExIsResourceAcquiredExclusiveLite(pFcb->PagingIoResource);
                if (bLockedPagingIoResource) {
                    if (bLockedResource == FALSE)
                        isPagingIoResourceLockedFirst = TRUE;
                    bNeedReleasePagingIoResource = TRUE;
                }
            }
        }

        //
        // ʹ����, ������, һ����.....
        //
        if (pFcb->Resource) {
            if (bLockedResource == FALSE) {
                //
                // �ȳ�����һ����Դ
                //
                if (ExIsResourceAcquiredExclusiveLite(pFcb->Resource) == FALSE) {
                    //
                    // û�õ���Դ, ����һ��.
                    //
                    if (bLockedPagingIoResource) {
                        if (ExAcquireResourceExclusiveLite(pFcb->Resource, FALSE) == FALSE) {
                            bBreak = FALSE;
                            bLockedResource = FALSE;
                            bNeedReleaseResource = FALSE;
                        }
                        else {
                            bLockedResource = TRUE;
                            bNeedReleaseResource = TRUE;
                        }
                    }
                    else {
                        if (bLockedResource == FALSE) {
                            ExAcquireResourceExclusiveLite(pFcb->Resource, TRUE);
                            bLockedResource = TRUE;
                            bNeedReleaseResource = TRUE;
                            isPagingIoResourceLockedFirst = FALSE;
                        }
                    }
                }
                else {
                    bLockedResource = TRUE;
                    bNeedReleaseResource = TRUE;
                }
            }
        }

        if (pFcb->PagingIoResource) {
            if (bLockedPagingIoResource == FALSE) {
                //
                // ������ PagingIoResource ����Դ
                //
                if (bLockedResource) {
                    if (ExAcquireResourceExclusiveLite(pFcb->PagingIoResource, FALSE) == FALSE) {
                        bBreak = FALSE;
                        bLockedPagingIoResource = FALSE;
                        bNeedReleasePagingIoResource = FALSE;
                    }
                    else {
                        if (bLockedResource == FALSE)
                            isPagingIoResourceLockedFirst = TRUE;
                        bLockedPagingIoResource = TRUE;
                        bNeedReleasePagingIoResource = TRUE;
                    }
                }
                else {
                    if (bLockedPagingIoResource == FALSE) {
                        ExAcquireResourceExclusiveLite(pFcb->PagingIoResource, TRUE);
                        if (bLockedResource == FALSE)
                            isPagingIoResourceLockedFirst = TRUE;
                        bLockedPagingIoResource = TRUE;
                        bNeedReleasePagingIoResource = TRUE;
                    }
                }
            }
        }

        if (bLockedResource && bLockedPagingIoResource) {
            break;
        }

        if (bBreak) {
            break;
        }

#if defined(FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK) && (FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK != 0)
        if (isPagingIoResourceLockedFirst) {
            if (bNeedReleasePagingIoResource) {
                if (pFcb->PagingIoResource)
                    ExReleaseResourceLite(pFcb->PagingIoResource);
                bLockedPagingIoResource = FALSE;
                bNeedReleasePagingIoResource = FALSE;
            }
            if (bNeedReleaseResource) {
                if (pFcb->Resource)
                    ExReleaseResourceLite(pFcb->Resource);
                bLockedResource = TRUE;
                bNeedReleaseResource = TRUE;
            }
        }
        else {
            if (bNeedReleaseResource) {
                if (pFcb->Resource)
                    ExReleaseResourceLite(pFcb->Resource);
                bLockedResource = TRUE;
                bNeedReleaseResource = TRUE;
            }
            if (bNeedReleasePagingIoResource) {
                if (pFcb->PagingIoResource)
                    ExReleaseResourceLite(pFcb->PagingIoResource);
                bLockedPagingIoResource = FALSE;
                bNeedReleasePagingIoResource = FALSE;
            }
        }
        isPagingIoResourceLockedFirst = FALSE;
#endif

        /*
        if (irql == PASSIVE_LEVEL) {
//          FsRtlExitFileSystem();
            KeDelayExecutionThread(KernelMode, FALSE, &liInterval);
        }
        else {
            KEVENT waitEvent;
            KeInitializeEvent(&waitEvent, NotificationEvent, FALSE);
            KeWaitForSingleObject(&waitEvent, Executive, KernelMode, FALSE, &liInterval);
        }
        */
    }

#else // !FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK

    if (pFcb->PagingIoResource) {
        ExAcquireResourceExclusiveLite(pFcb->PagingIoResource, TRUE);
        bLockedPagingIoResource = TRUE;
    }

#endif // FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK

    //
    // �����õ�����
    //
    if (pFileObject->SectionObjectPointer) {
        IO_STATUS_BLOCK ioStatus;
        IoSetTopLevelIrp((PIRP)FSRTL_FSP_TOP_LEVEL_IRP);
        CcFlushCache(pFileObject->SectionObjectPointer, NULL, 0, &ioStatus);

        if (pFileObject->SectionObjectPointer->ImageSectionObject) {
            MmFlushImageSection(pFileObject->SectionObjectPointer, MmFlushForWrite); // MmFlushForDelete()
        }

        CcPurgeCacheSection(pFileObject->SectionObjectPointer, NULL, 0, FALSE);
        IoSetTopLevelIrp(NULL);
    }

#if defined(FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK) && (FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK != 0)
    if (isPagingIoResourceLockedFirst) {
        if (bNeedReleasePagingIoResource) {
            if (pFcb->PagingIoResource)
                ExReleaseResourceLite(pFcb->PagingIoResource);
        }
        if (bNeedReleaseResource) {
            if (pFcb->Resource)
                ExReleaseResourceLite(pFcb->Resource);
        }
    }
    else {
        if (bNeedReleaseResource) {
            if (pFcb->Resource)
                ExReleaseResourceLite(pFcb->Resource);
        }
        if (bNeedReleasePagingIoResource) {
            if (pFcb->PagingIoResource)
                ExReleaseResourceLite(pFcb->PagingIoResource);
        }
    }
#else // !FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK
    if (bLockedPagingIoResource == TRUE) {
        if (pFcb->PagingIoResource != NULL) {
            ExReleaseResourceLite(pFcb->PagingIoResource);
        }
        bLockedPagingIoResource = FALSE;
    }
#endif // FILE_CLEAR_CACHE_USE_ORIGINAL_LOCK

    FsRtlExitFileSystem();
    /*
    Acquire:
        FsRtlEnterFileSystem();

        if (Fcb->Resource)
            ResourceAcquired = ExAcquireResourceExclusiveLite(Fcb->Resource, TRUE);
        if (Fcb->PagingIoResource)
            PagingIoResourceAcquired = ExAcquireResourceExclusive(Fcb->PagingIoResource, FALSE);
        else
            PagingIoResourceAcquired = TRUE ;
        if (!PagingIoResourceAcquired) {
            if (Fcb->Resource)  ExReleaseResource(Fcb->Resource);
            FsRtlExitFileSystem();
            KeDelayExecutionThread(KernelMode,FALSE,&Delay50Milliseconds);
            goto Acquire;
        }

        if (FileObject->SectionObjectPointer) {
            IoSetTopLevelIrp( (PIRP)FSRTL_FSP_TOP_LEVEL_IRP);

            if (bIsFlushCache) {
                CcFlushCache( FileObject->SectionObjectPointer, FileOffset, Length, &IoStatus);
            }

            if (FileObject->SectionObjectPointer->ImageSectionObject) {
                MmFlushImageSection(
                    FileObject->SectionObjectPointer,
                    MmFlushForWrite);
            }

            if (FileObject->SectionObjectPointer->DataSectionObject) {
                PurgeRes = CcPurgeCacheSection(FileObject->SectionObjectPointer,
                    NULL,
                    0,
                    FALSE);
            }

            IoSetTopLevelIrp(NULL);
        }

        if (Fcb->PagingIoResource)
            ExReleaseResourceLite(Fcb->PagingIoResource);

        if (Fcb->Resource)
            ExReleaseResourceLite(Fcb->Resource);

        FsRtlExitFileSystem();
        */
}