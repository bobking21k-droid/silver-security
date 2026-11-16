/*
 * Minimal UEFI definitions for silverboot, written from the UEFI 2.10 spec.
 * Only what the chooser uses; struct layouts must match the spec exactly.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint64_t UINTN;
typedef int64_t INTN;
typedef uint16_t CHAR16;
typedef uint8_t BOOLEAN;
typedef UINTN EFI_STATUS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_ERROR(s) (((INTN)(s)) < 0)
#define EFI_NOT_READY (0x8000000000000006ULL)

typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } EFI_GUID;

typedef struct {
	uint64_t Signature;
	uint32_t Revision, HeaderSize, CRC32, Reserved;
} EFI_TABLE_HEADER;

/* ---- console input ---- */
typedef struct { uint16_t ScanCode; CHAR16 UnicodeChar; } EFI_INPUT_KEY;
typedef struct EFI_SIMPLE_TEXT_INPUT {
	EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_INPUT *, BOOLEAN);
	EFI_STATUS (EFIAPI *ReadKeyStroke)(struct EFI_SIMPLE_TEXT_INPUT *, EFI_INPUT_KEY *);
	EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT;
#define SCAN_UP 0x01
#define SCAN_DOWN 0x02
#define SCAN_RIGHT 0x03
#define SCAN_LEFT 0x04
#define SCAN_ESC 0x17

typedef struct EFI_SIMPLE_TEXT_OUTPUT {
	void *Reset;
	EFI_STATUS (EFIAPI *OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT *, CHAR16 *);
	void *TestString, *QueryMode, *SetMode, *SetAttribute;
	EFI_STATUS (EFIAPI *ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT *);
	void *SetCursorPosition;
	EFI_STATUS (EFIAPI *EnableCursor)(struct EFI_SIMPLE_TEXT_OUTPUT *, BOOLEAN);
	void *Mode;
} EFI_SIMPLE_TEXT_OUTPUT;

/* ---- runtime services: only the variable reader and writer are used ---- */
typedef struct {
	EFI_TABLE_HEADER Hdr;
	void *GetTime, *SetTime, *GetWakeupTime, *SetWakeupTime, *SetVirtualAddressMap, *ConvertPointer;
	EFI_STATUS (EFIAPI *GetVariable)(CHAR16 *Name, EFI_GUID *Vendor, uint32_t *Attributes, UINTN *DataSize, void *Data);
	void *GetNextVariableName;
	EFI_STATUS (EFIAPI *SetVariable)(CHAR16 *Name, EFI_GUID *Vendor, uint32_t Attributes, UINTN DataSize, void *Data);
} EFI_RUNTIME_SERVICES;

/* ---- boot services (order is significant) ---- */
typedef struct {
	EFI_TABLE_HEADER Hdr;
	void *RaiseTPL, *RestoreTPL;
	void *AllocatePages, *FreePages;
	EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *Size, void *Map, UINTN *Key, UINTN *DescSize, uint32_t *Ver);
	EFI_STATUS (EFIAPI *AllocatePool)(uint32_t PoolType, UINTN Size, void **Buffer);
	EFI_STATUS (EFIAPI *FreePool)(void *Buffer);
	void *CreateEvent, *SetTimer, *WaitForEvent, *SignalEvent, *CloseEvent, *CheckEvent;
	void *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
	EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
	void *Reserved;
	void *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath, *InstallConfigurationTable;
	EFI_STATUS (EFIAPI *LoadImage)(BOOLEAN BootPolicy, EFI_HANDLE Parent, void *DevicePath,
				       void *SourceBuffer, UINTN SourceSize, EFI_HANDLE *Image);
	EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE, UINTN *ExitDataSize, CHAR16 **ExitData);
	EFI_STATUS (EFIAPI *Exit)(EFI_HANDLE, EFI_STATUS, UINTN, CHAR16 *);
	void *UnloadImage, *ExitBootServices, *GetNextMonotonicCount;
	EFI_STATUS (EFIAPI *Stall)(UINTN Microseconds);
	EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN Timeout, uint64_t Code, UINTN Size, CHAR16 *Data);
	void *ConnectController, *DisconnectController;
	void *OpenProtocol, *CloseProtocol, *OpenProtocolInformation, *ProtocolsPerHandle;
	EFI_STATUS (EFIAPI *LocateHandleBuffer)(uint32_t SearchType, EFI_GUID *, void *Key,
						UINTN *NoHandles, EFI_HANDLE **Buffer);
	EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *, void *Registration, void **Interface);
	void *InstallMultipleProtocolInterfaces, *UninstallMultipleProtocolInterfaces;
	void *CalculateCrc32, *CopyMem, *SetMem, *CreateEventEx;
} EFI_BOOT_SERVICES;
#define EfiLoaderData 2
#define ByProtocol 2

typedef struct {
	EFI_TABLE_HEADER Hdr;
	CHAR16 *FirmwareVendor;
	uint32_t FirmwareRevision;
	EFI_HANDLE ConsoleInHandle;
	EFI_SIMPLE_TEXT_INPUT *ConIn;
	EFI_HANDLE ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT *ConOut;
	EFI_HANDLE StandardErrorHandle;
	EFI_SIMPLE_TEXT_OUTPUT *StdErr;
	void *RuntimeServices;
	EFI_BOOT_SERVICES *BootServices;
	UINTN NumberOfTableEntries;
	void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ---- graphics output ---- */
typedef struct {
	uint32_t Version, HorizontalResolution, VerticalResolution, PixelFormat;
	uint32_t Mask[4];
	uint32_t PixelsPerScanLine;
} EFI_GOP_MODE_INFO;
typedef struct {
	uint32_t MaxMode, Mode;
	EFI_GOP_MODE_INFO *Info;
	UINTN SizeOfInfo;
	uint64_t FrameBufferBase;
	UINTN FrameBufferSize;
} EFI_GOP_MODE;
typedef struct EFI_GOP {
	void *QueryMode, *SetMode;
	EFI_STATUS (EFIAPI *Blt)(struct EFI_GOP *, uint32_t *Buffer, uint32_t Op, UINTN SrcX, UINTN SrcY,
				 UINTN DstX, UINTN DstY, UINTN W, UINTN H, UINTN Delta);
	EFI_GOP_MODE *Mode;
} EFI_GOP;
#define EfiBltBufferToVideo 2

/* ---- pointers (mouse / touchpad / tablet) ---- */
typedef struct { int32_t RelX, RelY, RelZ; BOOLEAN Left, Right; } EFI_POINTER_STATE;
typedef struct { uint64_t ResX, ResY, ResZ; BOOLEAN Left, Right; } EFI_POINTER_MODE;
typedef struct EFI_POINTER {
	void *Reset;
	EFI_STATUS (EFIAPI *GetState)(struct EFI_POINTER *, EFI_POINTER_STATE *);
	EFI_EVENT WaitForInput;
	EFI_POINTER_MODE *Mode;
} EFI_POINTER;

typedef struct { uint64_t X, Y, Z; uint32_t Buttons; } EFI_ABS_STATE;
typedef struct { uint64_t MinX, MinY, MinZ, MaxX, MaxY, MaxZ; uint32_t Attributes; } EFI_ABS_MODE;
typedef struct EFI_ABS_POINTER {
	void *Reset;
	EFI_STATUS (EFIAPI *GetState)(struct EFI_ABS_POINTER *, EFI_ABS_STATE *);
	EFI_EVENT WaitForInput;
	EFI_ABS_MODE *Mode;
} EFI_ABS_POINTER;

/* ---- files ---- */
typedef struct EFI_FILE {
	uint64_t Revision;
	EFI_STATUS (EFIAPI *Open)(struct EFI_FILE *, struct EFI_FILE **, CHAR16 *, uint64_t Mode, uint64_t Attr);
	EFI_STATUS (EFIAPI *Close)(struct EFI_FILE *);
	void *Delete;
	EFI_STATUS (EFIAPI *Read)(struct EFI_FILE *, UINTN *, void *);
	void *Write;
	EFI_STATUS (EFIAPI *GetPosition)(struct EFI_FILE *, uint64_t *);
	EFI_STATUS (EFIAPI *SetPosition)(struct EFI_FILE *, uint64_t);
} EFI_FILE;
typedef struct EFI_SFS {
	uint64_t Revision;
	EFI_STATUS (EFIAPI *OpenVolume)(struct EFI_SFS *, EFI_FILE **);
} EFI_SFS;
#define EFI_FILE_MODE_READ 1

typedef struct {
	uint32_t Revision;
	EFI_HANDLE ParentHandle;
	EFI_SYSTEM_TABLE *SystemTable;
	EFI_HANDLE DeviceHandle;
	void *FilePath;
} EFI_LOADED_IMAGE;

typedef struct { uint8_t Type, SubType, Length[2]; } EFI_DEVICE_PATH;

#define GOP_GUID        { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }
#define POINTER_GUID    { 0x31878c87, 0x0b75, 0x11d5, { 0x9a, 0x4f, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
#define ABSPOINTER_GUID { 0x8d59d32b, 0xc655, 0x4ae9, { 0x9b, 0x15, 0xf2, 0x59, 0x04, 0x99, 0x2a, 0x43 } }
#define SFS_GUID        { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define LOADED_IMAGE_GUID { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define DEVICE_PATH_GUID  { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
