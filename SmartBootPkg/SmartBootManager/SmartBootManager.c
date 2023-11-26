#include "./SmartBootManager.h"

UINT8 lookUpTable[256];

EFI_STATUS WriteReadData(IN EFI_SERIAL_IO_PROTOCOL *pSerialIo, IN OUT UINT8 **buffer, IN OUT UINTN *bufferSize, UINT64 timeout);
EFI_STATUS ReallocateMemory(UINT8 **buffer, UINTN oldSize, UINTN newSize);

VOID PrintData(UINT8 *buffer, UINTN bufferSize)
{
	for (UINTN i = 0; i < bufferSize; i++)
	{
		Print(L"0x%x\n", buffer[i]);
	}
}

VOID ClearSerialInput(EFI_SERIAL_IO_PROTOCOL *pSerialIo)
{
	EFI_STATUS status;
	UINTN bufferSize;
	CHAR8 *buffer;

	bufferSize = 1;
	buffer = NULL;

	status = gBS->AllocatePool(EfiBootServicesData, bufferSize, (VOID **)&buffer);
	if (EFI_ERROR(status))
	{
		Print(L"Error with memory allocation\n");
		return;
	}

	while (bufferSize)
	{
		pSerialIo->Read(pSerialIo, &bufferSize, buffer);
	}
}

EFI_SERIAL_IO_PROTOCOL *GetSerialProtocol()
{
	EFI_STATUS status;
	UINTN nSerialFound;
	EFI_HANDLE *hSerial;
	EFI_DEVICE_PATH_PROTOCOL *pDevicePath;
	EFI_SERIAL_IO_PROTOCOL *pSerialIo;
	UINTN bufferSize;
	UINT8 *buffer; 
	EFI_EVENT eTimer;

	bufferSize = 2;
	eTimer = NULL;
	pSerialIo = NULL;
	pDevicePath = NULL;
	hSerial = NULL;

	status = gBS->AllocatePool(EfiBootServicesData, bufferSize, (VOID**) &buffer);
	if (EFI_ERROR(status))
	{
		Print(L"Error with memory allocation\n");
		return NULL;
	}

	status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &eTimer);
	if (EFI_ERROR(status))
	{
		Print(L"Error with event\n");
		return NULL;
	}

	status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSerialIoProtocolGuid, NULL, &nSerialFound, &hSerial);
	if EFI_ERROR (status)
	{
		return NULL;
	}

	if (nSerialFound > 1)
	{
		Print(L"Warning, %d serial devices found\n", nSerialFound);
	}

	for (int i = 0; i < PING_PONG_RETRIES; i++)
	{
		//Print(L"Ping #%d\n", i + 1);
		for (UINTN i = 0; i < nSerialFound; i++)
		{
			status = gBS->HandleProtocol(hSerial[i], &gEfiDevicePathProtocolGuid, (VOID **)&pDevicePath);
			if (EFI_ERROR(status) || pDevicePath->Type != 2 || pDevicePath->SubType != 1)
			{
				continue;
			}

			status = gBS->HandleProtocol(hSerial[i], &gEfiSerialIoProtocolGuid, (VOID **)&pSerialIo);
			if (EFI_ERROR(status))
			{
				continue;
			}

			buffer[0] = CONTROL_DATA;
			buffer[1] = PING;
			status = WriteReadData(pSerialIo, &buffer, &bufferSize, DEFAULT_TIMEOUT);

			if (EFI_ERROR(status) || bufferSize != 2)
			{
				bufferSize = 2;
				ReallocateMemory(&buffer, 0, bufferSize);
				continue;
			}

			if (buffer[1] == PONG)
			{
				Print(L"Ping-pong\n");
				gBS->FreePool(hSerial);
				return pSerialIo;
			}
		}
	}

	gBS->FreePool(hSerial);
	return NULL;
}

UINT8 GetCrc8(UINT8 *buffer, UINTN bufferSize)
{
	UINT8 crc = 0;
	UINT8 byte;
	for (UINTN i = 0; i < bufferSize; i++)
	{
		byte = buffer[i] ^ crc;
		crc = lookUpTable[byte];
	}

	return crc;
}

EFI_STATUS ConnectAllEfi(VOID)
{
	EFI_STATUS Status;
	UINTN HandleCount;
	EFI_HANDLE *HandleBuffer;
	UINTN Index;

	Status = gBS->LocateHandleBuffer(
		AllHandles,
		NULL,
		NULL,
		&HandleCount,
		&HandleBuffer);
	if (EFI_ERROR(Status))
	{
		return Status;
	}

	for (Index = 0; Index < HandleCount; Index++)
	{
		Status = gBS->ConnectController(HandleBuffer[Index], NULL, NULL, TRUE);
	}

	if (HandleBuffer != NULL)
	{
		gBS->FreePool(HandleBuffer);
	}

	return EFI_SUCCESS;
}

EFI_STATUS LoadDriver(IN EFI_HANDLE ImageHandle)
{
	EFI_STATUS status;
	EFI_LOADED_IMAGE_PROTOCOL *LoadedImageProtocolPtr;
	UINTN bufferSize;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Volume;
	EFI_FILE_HANDLE File;
	EFI_FILE_PROTOCOL *token;
	EFI_HANDLE LoadedDriverHandle;
	UINT8 *buffer;

	bufferSize = 21184;
	token = NULL;

	status = gBS->OpenProtocol(
		ImageHandle,
		&gEfiLoadedImageProtocolGuid,
		(VOID **)&LoadedImageProtocolPtr,
		ImageHandle,
		NULL,
		EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(status))
	{
		Print(L"Error, Open EfiLoadedImageProtocol\n");
		return status;
	}

	File = NULL;
	status = gBS->HandleProtocol(
		LoadedImageProtocolPtr->DeviceHandle,
		&gEfiSimpleFileSystemProtocolGuid,
		(VOID *)&Volume);

	if (EFI_ERROR(status))
	{
		Print(L"Error, get volume\n");
		return status;
	}

	status = Volume->OpenVolume(
		Volume,
		&File);
	if (EFI_ERROR(status))
	{
		Print(L"Error, OpenVolume\n");
		return status;
	}

	status = File->Open(
		File,
		&token,
		L"FtdiUsbSerialDxe.efi",
		EFI_FILE_MODE_READ,
		EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM);
	if (EFI_ERROR(status))
	{
		Print(L"Error while opening file\n");
		return status;
	}

	status = gBS->AllocatePool(EfiBootServicesData, bufferSize, (VOID **)&buffer);
	if (EFI_ERROR(status))
	{
		Print(L"Error with memory allocation\n");
		return status;
	}

	status = token->Read(token, &bufferSize, buffer);
	if (EFI_ERROR(status))
	{
		Print(L"Error with file Read\n");
		return status;
	}

	status = gBS->LoadImage(
		FALSE,
		ImageHandle,
		NULL,
		buffer,
		bufferSize,
		&LoadedDriverHandle);
	if (EFI_ERROR(status))
	{
		Print(L"Error with LoadImage");
		return status;
	}

	status = gBS->StartImage(LoadedDriverHandle, NULL, NULL);
	if (EFI_ERROR(status))
	{
		Print(L"Error with StartImage\n");
		return status;
	}

	return EFI_SUCCESS;
}

BOOLEAN IgnoreBootOption(IN EFI_BOOT_MANAGER_LOAD_OPTION *BootOption)
{
	EFI_STATUS Status;
	EFI_DEVICE_PATH_PROTOCOL *ImageDevicePath;

	//
	// Ignore myself.
	//
	Status = gBS->HandleProtocol(gImageHandle, &gEfiLoadedImageDevicePathProtocolGuid, (VOID **)&ImageDevicePath);
	if (EFI_ERROR(Status))
	{
		Print(L"Error with gEfiLoadedImageDevicePathProtocolGuid\n");
		return Status;
	}

	if (CompareMem(BootOption->FilePath, ImageDevicePath, GetDevicePathSize(ImageDevicePath)) == 0)
	{
		return TRUE;
	}

	//
	// Ignore the hidden/inactive boot option.
	//
	if (((BootOption->Attributes & LOAD_OPTION_HIDDEN) != 0) || ((BootOption->Attributes & LOAD_OPTION_ACTIVE) == 0))
	{
		return TRUE;
	}

	return FALSE;
}

VOID BootFromSelectOption(IN EFI_BOOT_MANAGER_LOAD_OPTION *BootOptions, IN UINTN BootOptionCount, IN UINTN SelectItem)
{
	UINTN ItemNum;
	UINTN Index;

	if (BootOptions == NULL)
	{
		Print(L"Error, BootOptions == NULL\n");
		return;
	}

	for (ItemNum = 0, Index = 0; Index < BootOptionCount; Index++)
	{
		if (IgnoreBootOption(&BootOptions[Index]))
		{
			continue;
		}

		if (ItemNum++ == SelectItem)
		{
			EfiBootManagerBoot(&BootOptions[Index]);
			break;
		}
	}
}

UINT8 GetNumberOfSetBits(UINT8 byte)
{
	UINT8 bitCounter = 0;
	UINT8 bit;
	for (UINT8 counter = 0; counter < 8; counter++)
	{
		bit = 1 << counter;
		if (bit & byte)
		{
			bitCounter++;
		}
	}

	return bitCounter;
}

EFI_STATUS WriteData(EFI_SERIAL_IO_PROTOCOL *pSerialIo, UINT8 *packet, IN OUT UINTN packetSize)
{
	EFI_STATUS status;
	UINT8 receiveBuffer[1];
	EFI_EVENT eTimer = NULL;
	UINTN responseDataCount = 1;

	status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &eTimer);
	if (EFI_ERROR(status))
	{
		Print(L"Error with event\n");
		return status;
	}

	UINTN bytesToSend;
	for (UINT8 counter = 0; counter < WRITE_READ_MAX_TRY_COUNT; counter++)
	{
		//Print(L"Write data #%u\n", counter);
		bytesToSend = packetSize;
		pSerialIo->Write(pSerialIo, &bytesToSend, packet);
		if (bytesToSend == 0)
		{
			continue;
		}

		if (ACK_TIMEOUT != 0)
		{
			gBS->SetTimer(eTimer, TimerRelative, ACK_TIMEOUT);	// wait up to 5s for ACK 
		}
		
		while (TRUE)
		{
			responseDataCount = 1; // ACK/NACK is 1 byte
			pSerialIo->Read(pSerialIo, &responseDataCount, receiveBuffer);
			if (responseDataCount == 1)
			{
				if (GetNumberOfSetBits(receiveBuffer[0]) < 4)
				{
					//Print(L"ACK received\n");
					return EFI_SUCCESS;
				}
				else if (GetNumberOfSetBits(receiveBuffer[0]) > 4)
				{
					//Print(L"NACK received\n");
					break;
				}
				else
				{
					Print(L"Neither ACK nor NACK received - FATAL ERROR\n");
					return EFI_UNSUPPORTED;
				}
			}
			else if (gBS->CheckEvent(eTimer) == EFI_SUCCESS)
			{
				Print(L"ACK receive timeout\n");
				return EFI_TIMEOUT;
			}
		}
	}

	Print(L"%d invalid trials\n", WRITE_READ_MAX_TRY_COUNT);
	return EFI_UNSUPPORTED;
}

EFI_STATUS WriteAckData(EFI_SERIAL_IO_PROTOCOL *pSerialIo, UINT8 data)
{
	UINT8 *packet;
	UINTN packetSize = 1;
	EFI_STATUS status;
	EFI_EVENT eTimer = NULL;
	UINTN index;

	if (data != ACK && data != NACK)
	{
		Print(L"Invalid ACK data type\n");
		return EFI_UNSUPPORTED;
	}

	status = gBS->AllocatePool(EfiBootServicesData, packetSize, (VOID **)&packet);
	if (EFI_ERROR(status))
	{
		Print(L"Error with memory allocation\n");
		return status;
	}

	if (data == NACK)
	{
		status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &eTimer);
		if (EFI_ERROR(status))
		{
			Print(L"Error with event\n");
			return status;
		}

		gBS->SetTimer(eTimer, TimerRelative, WAIT_FOR_INPUT_TIMEOUT);
		gBS->WaitForEvent(1, &eTimer, &index);
		ClearSerialInput(pSerialIo);
	}

	packet[0] = data;

	for (UINT8 counter = 0; counter < WRITE_READ_MAX_TRY_COUNT; counter++)
	{
		packetSize = 1;
		pSerialIo->Write(pSerialIo, &packetSize, packet);
		if (packetSize == 1)
		{
			return EFI_SUCCESS;
		}
	}

	return EFI_NOT_READY;
}

EFI_STATUS ReallocateMemory(UINT8 **buffer, UINTN oldSize, UINTN newSize)
{
	EFI_STATUS status;
	UINT8 *newBuffer;

	status = gBS->AllocatePool(EfiBootServicesData, newSize, (VOID **)&newBuffer);
	if (EFI_ERROR(status))
	{
		gBS->FreePool(*buffer);
		Print(L"Error with memory allocation\n");
		return status;
	}

	if (oldSize != 0)
	{
		CopyMem(newBuffer, *buffer, oldSize);
		gBS->FreePool(*buffer);
	}
	*buffer = newBuffer;

	return EFI_SUCCESS;
}

//timeout = 10000000 = 1s		timeout = 0 = infinite wait			timeout = 0xFFFFFFFFFFFFFFFF = ACK/NACK only	buffer is free after sending
EFI_STATUS WriteReadData(IN EFI_SERIAL_IO_PROTOCOL *pSerialIo, UINT8 **buffer, IN OUT UINTN *bufferSize, UINT64 timeout)
{
	EFI_STATUS status;
	UINT8 *packet;
	UINT8 packetSize;
	EFI_EVENT eTimer = NULL;
	UINT8 sentDataType = *buffer[0];
	if (sentDataType == DATA)
	{
		packetSize = *bufferSize + 4;	// DataType, PacketLength x3, buffer, CRC8
		status = gBS->AllocatePool(EfiBootServicesData, packetSize, (VOID **)&packet);
		if (EFI_ERROR(status))
		{
			Print(L"Error with memory allocation\n");
			return status;
		}

		packet[0] = sentDataType;
		packet[1] = packetSize;
		packet[2] = packetSize;
		packet[3] = packetSize;
		CopyMem(&packet[4], &((*buffer)[1]), *bufferSize - 1);
		packet[packetSize - 1] = GetCrc8(packet, packetSize - 1);
	}
	else
	{
		if (*bufferSize != 2)
		{
			Print(L"Invalid length of control data\n");
			return EFI_NOT_STARTED;
		}

		packetSize = 5;	//DataType, Data x3, CRC8
		status = gBS->AllocatePool(EfiBootServicesData, packetSize, (VOID **)&packet);
		if (EFI_ERROR(status))
		{
			Print(L"Error with memory allocation\n");
			return status;
		}

		packet[0] = sentDataType;
		packet[1] = (*buffer)[1];
		packet[2] = (*buffer)[1];
		packet[3] = (*buffer)[1];
		packet[4] = GetCrc8(packet, packetSize - 1);
	}

	status = WriteData(pSerialIo, packet, packetSize);
	gBS->FreePool(packet);
	gBS->FreePool(*buffer);

	if (EFI_ERROR(status))
	{
		Print(L"Error while sending data\n");
		return EFI_NOT_READY;
	}

	if (timeout == ACK_ONLY)
	{
		return EFI_SUCCESS;
	}

	status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &eTimer);
	if (EFI_ERROR(status))
	{
		Print(L"Error with event\n");
		return status;
	}

	packetSize = 5;
	status = gBS->AllocatePool(EfiBootServicesData, packetSize, (VOID **)&packet);
	if (EFI_ERROR(status))
	{
		Print(L"Error with memory allocation\n");
		return status;
	}

	for (UINT8 counter = 0; counter < WRITE_READ_MAX_TRY_COUNT; counter++)
	{
		UINTN bytesRead = 0;
		UINTN sizeAvailable;
		if (timeout != 0)
		{
			gBS->SetTimer(eTimer, TimerRelative, timeout);
		}

		while (TRUE)
		{
			sizeAvailable = packetSize - bytesRead;
			pSerialIo->Read(pSerialIo, &sizeAvailable, &packet[bytesRead]);
			if (sizeAvailable != 0)	//if data was received
			{
				bytesRead += sizeAvailable;
				if (packet[0] == CONTROL_DATA && bytesRead == 5)	//if whole control data was received
				{
					if (packet[1] == packet[2] && packet[2] == packet[3] && GetCrc8(packet, packetSize - 1) == packet[4])
					{
						*bufferSize = 2;
						status = gBS->AllocatePool(EfiBootServicesData, *bufferSize, (VOID **)buffer);
						if (EFI_ERROR(status))
						{
							Print(L"Error with memory allocation\n");
							gBS->FreePool(packet);
							return status;
						}
						
						(*buffer)[0] = packet[0];
						(*buffer)[1] = packet[1];
						gBS->FreePool(packet);
						if (WriteAckData(pSerialIo, ACK) != EFI_SUCCESS)
						{

							Print(L"Error sending NACK\n");
							return EFI_NOT_READY;
						}

						return EFI_SUCCESS;
					}
					else
					{
						Print(L"Invalid packet received. Sending NACK\n");
						if (WriteAckData(pSerialIo, NACK) != EFI_SUCCESS)
						{
							Print(L"Error sending NACK\n");
							gBS->FreePool(packet);
							return EFI_NOT_READY;
						}

						break;
					}
				}
				else if (packet[0] == DATA && bytesRead > 4)	//if some of DATA was received
				{
					if (packet[1] == packet[2] && packet[2] == packet[3])
					{	
						if (packetSize < packet[1])
						{
							if (ReallocateMemory(&packet, packetSize, packet[1]) != EFI_SUCCESS)
							{
								Print(L"Error reallocating memory\n");
								gBS->FreePool(packet);
								return EFI_NOT_READY;
							}

							packetSize = packet[1];
						}

						if (bytesRead == packet[1])
						{
							if (packet[packetSize - 1] == GetCrc8(packet, packetSize - 1))
							{
								*bufferSize = packetSize - 4;
								status = gBS->AllocatePool(EfiBootServicesData, *bufferSize, (VOID **)buffer);
								if (EFI_ERROR(status))
								{
									Print(L"Error with memory allocation\n");
									gBS->FreePool(packet);
									return status;
								}

								CopyMem(&((*buffer)[1]), &packet[4], *bufferSize - 1);
								(*buffer)[0] = packet[0];
								gBS->FreePool(packet);
								if (WriteAckData(pSerialIo, ACK) != EFI_SUCCESS)
								{

									Print(L"Error sending NACK\n");
									return EFI_NOT_READY;
								}

								return EFI_SUCCESS;
							}
							else
							{
								Print(L"Invalid CRC\n");
								if (WriteAckData(pSerialIo, NACK) != EFI_SUCCESS)
								{
									Print(L"Error sending NACK\n");
									gBS->FreePool(packet);
									return EFI_NOT_READY;
								}

								break;
							}
						}
					}
					else
					{
						Print(L"Invalid packet received. Sending NACK\n");
						if (WriteAckData(pSerialIo, NACK) != EFI_SUCCESS)
						{
							Print(L"Error sending NACK\n");
							gBS->FreePool(packet);
							return EFI_NOT_READY;
						}

						break;
					}
				}
				else if (packet[0] != DATA && packet[0] != CONTROL_DATA)
				{
					if (WriteAckData(pSerialIo, NACK) != EFI_SUCCESS)
					{
						Print(L"Error sending NACK\n");
						gBS->FreePool(packet);
						return EFI_NOT_READY;
					}

					break;
				}
			}
			else if (timeout != 0)
			{
				if (gBS->CheckEvent(eTimer) == EFI_SUCCESS)
				{
					Print(L"Receive timeout\n");
					gBS->FreePool(packet);
					return EFI_TIMEOUT;
				}
			}
		}
	}

	Print(L"Max read trials exceeded");

	return EFI_UNSUPPORTED;
}

VOID CountErrors(EFI_SERIAL_IO_PROTOCOL *pSerialIo)
{
	EFI_STATUS status;
	UINTN bufferSize = 2;
	UINT8 counter = 0;
	UINTN nMistakes = 0;
	UINTN index;
	UINT8 *buffer;
	EFI_EVENT eTimer;

	status = gBS->AllocatePool(EfiBootServicesData, bufferSize, (VOID **)&buffer);
	if (EFI_ERROR(status))
	{
		Print(L"Error with memory allocation\n");
		return;
	}

	buffer[0] = CONTROL_DATA;
	for (UINTN i = 0; i < 100000; i++)
	{
		if (i % 1000 == 0)
		{
			Print(L"%u\n", i);
		}
		bufferSize = 1;
		if (++counter == 0)
		{
			++counter;
		} 

		buffer[1] = counter;
		//Print(L"sent %d ", counter);
		status = WriteReadData(pSerialIo, &buffer, &bufferSize, 1000000);
		if (status == EFI_SUCCESS)
		{
			//Print(L"- received %u\n", buffer[0]);
			if (buffer[0] != counter)
			{
				Print(L"MISTAKE!\n");
				Print(L"sent %u - received %u\n", counter, buffer[0]);
				gBS->SetTimer(eTimer, TimerRelative, 50000000); //5s
				status = gBS->WaitForEvent(1, eTimer, &index);
				nMistakes++;
			}
		}
		else
		{
			Print(L"\nEvent error\n");
		}
		
	}
	Print(L"Found %d mistakes\n", nMistakes);

	gBS->SetTimer(eTimer, TimerRelative, 50000000); //5s
	status = gBS->WaitForEvent(1, eTimer, &index);
}

VOID GenerateCrcTable()
{
	CONST UINT8 generator = CRC_POLYNOMIAL;
	UINT8 currentByte;
	for (int divident = 0; divident < 256; divident ++)
	{
		currentByte = divident;
		for (UINT8 bit = 0; bit < 8; bit++)
		{
			if (currentByte & 0x80)
			{
				currentByte <<= 1;
				currentByte ^= generator;
			}
			else
			{
				currentByte <<= 1;
			}
		}

		lookUpTable[divident] = currentByte;
	}
}

UINTN GetUtf16StringLength(CHAR16 *input)
{
	UINTN counter = 0;
	while (input[counter] != 0)
	{
		counter++;
	}

	return counter + 1;
}

UINT8* GetAsciiBufferFromUtf16(CHAR16 *input)
{
	UINT8 *output;
	ReallocateMemory(&output, 0, GetUtf16StringLength(input) + 1);
	UINT8 counter = 0;
	output[0] = DATA;
	while (input[counter] != 0)
	{
		output[counter + 1] = (UINT8) input[counter];
		counter++;
	}

	output[counter + 1] = '\0';
	return output;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_EVENT eTimer;
	EFI_STATUS status;
	EFI_SERIAL_IO_PROTOCOL *pSerialIo;
	UINTN bufferSize;
	UINT8 *buffer;
	//UINTN index;
	EFI_BOOT_MANAGER_LOAD_OPTION *BootOption;
	UINTN nBootOptionCount;

	eTimer = NULL;
	pSerialIo = NULL;
	bufferSize = 1;

	GenerateCrcTable();

	status = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &eTimer);
	if (EFI_ERROR(status))
	{
		Print(L"Error with event\n");
		return status;
	}

	status = LoadDriver(ImageHandle);
	if (EFI_ERROR(status))
	{
		Print(L"Error with LoadingDriver\n");
		return status;
	}

	EfiBootManagerConnectAll();
	EfiBootManagerRefreshAllBootOption();

	pSerialIo = GetSerialProtocol();
	if (pSerialIo == NULL)
	{
		Print(L"Serial not found\n");
		return EFI_NOT_STARTED;
	}

	BootOption = EfiBootManagerGetLoadOptions(&nBootOptionCount, LoadOptionTypeBoot);

	while (TRUE)
	{
		bufferSize = 2;
		ReallocateMemory(&buffer, 0, bufferSize);

		buffer[0] = CONTROL_DATA;
		buffer[1] = DATA_REQUEST;

		WriteReadData(pSerialIo, &buffer, &bufferSize, 0);

		if (buffer[0] == DATA)
		{
			// gBS->SetTimer(eTimer, TimerRelative, 20000000); //2s
			// status = gBS->WaitForEvent(1, &eTimer, &index);
			if (buffer[1] == 0xFF)
			{
				Print(L"No option selected\n");
				return EFI_SUCCESS;
			}
			Print(L"\nTrying to boot from option: %d\n", buffer[1]);
			BootFromSelectOption(BootOption, nBootOptionCount, buffer[1]);
			Print(L"Unable to boot from selected option\n");
			return EFI_NOT_FOUND;
		}
		else if (buffer[0] == CONTROL_DATA && buffer[1] == GET_BOOT_OPT)
		{
			for (int i = 0; i < nBootOptionCount; i++)
			{
				if (IgnoreBootOption(&BootOption[i]))
				{
					continue;
				}

				UINT8 *string = GetAsciiBufferFromUtf16(BootOption[i].Description);
				UINTN stringLen = GetUtf16StringLength(BootOption[i].Description);
				stringLen++;	//DATA type at first byte
				WriteReadData(pSerialIo, &string, &stringLen, ACK_ONLY);
			}
		}
		else
		{
			return EFI_NOT_FOUND;
		}

		gBS->FreePool(buffer);
	}

	return EFI_SUCCESS;
}