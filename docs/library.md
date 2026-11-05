# LOMAX.EXE — library code (CRT, CTL3D, MFC)

Everything from 0x4388CA to 0x458DFF is code Lomax didn't write: Microsoft's C runtime, the static CTL3D library and MFC 4.2, linked into the EXE. The game's own code and data are in [decompilation.md](decompilation.md). This file names and describes every library function and global in the Ghidra project.

## Layout

| Range | What | Naming |
|---|---|---|
| 0x4388CA–0x44974F | MSVC 4.x/5.0 C runtime: startup, heap (small-block heap), stdio, locale, math (`_CI*` intrinsics), SEH and C++ exception handling. | Real CRT names (`_flsbuf`, `__sbh_alloc_block`, `__CxxFrameHandler`, `__init_time`, …). |
| 0x449750–0x44CD40 | **CTL3D32S**, the static CTL3D library that MFC's `CWinApp::Enable3dControlsStatic` links in (3-D dialog controls on Win95/NT 3.51). Data at 0x45D3C8–0x45D48A and 0x5A66E0–0x5A71E5. | Public entry points under their real names (`Ctl3dRegister`, `Ctl3dSubclassDlgEx`, …). Internals are `Ctl3d_*` named by behaviour: CTL3D's source isn't public. |
| 0x44CD40–0x458DFF | MFC 4.2 (release, static): CWnd/CDialog/CPropertySheet, GDI wrappers, handle maps, thread/module state, CWinApp, CCommandLineInfo, exceptions, collections. | `Class_Method` (`CWnd_FromHandle`, `CDC_scalar_dtor`, `CHandleMap_DeleteTemp`); AFX globals by their source names (`_afxThreadState`, `afxData`, `classCWnd`, `CWnd_messageMap`). |

## Conventions

- **Exception stubs:** like in game code, `<Owner>_EHHandler` is the 10-byte `mov eax, FuncInfo; jmp __CxxFrameHandler` stub and `<Owner>_UnwindN` the funclet for unwind state N. MFC functions use `__EH_prolog` (`mov eax, handler; call __EH_prolog`), so the handler address in that prologue identifies the owner. There are 119 handlers and 169 funclets.
- **Static initialisers:** each MFC global with a constructor has a CRT `$E` initialiser: a 3-instruction stub (`call <object>_init_ctor; jmp <object>_init_atexit`), the constructor half, and an atexit-registration half whose destructor thunk is `<object>_atexitDtor`.
- **Shared bodies:** identical functions folded by the linker are named for what they do; the plate comment lists the classes that share them.
- **Phantom game labels:** some data labels in the library's address range are really elements of game arrays that Ghidra created from indexed accesses (`g_LevelDefs[-1]`, `g_MixerSamples[1]`, …). They are named `<array>_<index>_<field>` (`_m1_` = index −1).

## Differences from Ghidra's Function ID

Function ID (Ghidra's library signature matcher) gives generic, conflicting or wrong names for parts of this code. The project's names differ as follows:

- **Class-qualified names:** where Function ID gives a generic or conflicting name (12× `CreateObject`, 18× `` `scalar_deleting_destructor' ``, `FID_conflict:` picks, …), the name includes the class, so 163 functions carry their class in the name.
- **Different identifications:** 0x451225 is `CWnd::GetStyle` (Function ID: `GetExStyle`). `__init_monetary` starts at 0x446750; 0x446778 is a tail of it, `___init_monetary_part2`. The CTL3D edit and list window-proc helpers are the other way round from Function ID's match. `CWinApp_Enable3dControlsStatic` is the static variant. `global_operator_new`/`global_operator_delete` (0x451A01/0x451A41) are distinct from CNoTrackObject's pair (0x457EB2/0x457ED0). `terminate` is marked no-return.
- **`DestructExceptionObject` (0x43CCE0–0x43CD50)** includes its epilogue at 0x43CD3A. The jump to it crosses the function's `__except` filter and handler funclets (0x43CD2A, 0x43CD32). Ghidra's Shared Return Calls analyzer, with its default option "Assume Contiguous Functions Only", treats a jump across another function as a tail call and splits the epilogue into a function of its own. Creating the function with that option off keeps it whole.

## Gaps and uncertain names

- **Two `Unwind0` tails don't disassemble:** at 0x457BFD and 0x457C7C, the 6-byte `call; ret` tails are typed as bytes.
- **Uncertain names:** 13 globals (marked UNCERTAIN or `U` in the tables) and 2 functions named by behaviour (`Mfc_GdiBrushTermObj_*`, probably `_afxHalftoneBrush`'s terminator).

<details><summary>Functions: CRT and CTL3D (0x439968–0x44A9E0) (108)</summary>

| Address | Name | What it is |
|---|---|---|
| 0x00439968 | CThreadLocal_AFX_THREAD_STATE_CreateObject | C. `new _AFX_THREAD_STATE` (0x114 bytes) |
| 0x004399c5 | CProcessLocal_AFX_CTL3D_STATE_CreateObject | C. new 0x30-byte object, vtable 0x45b01c (RTTI _AFX_CTL3D_STATE) |
| 0x00439a32 | CNoTrackObject_dtor | C. sets vptr to CNoTrackObject vtable 0x45b024; called from EH funclets |
| 0x00439a55 | _AFX_CTL3D_STATE_scalar_dtor | C. vtable 0x45b01c |
| 0x00439a9a | CListCtrl_RemoveImageList | C. LVM_GET/SETIMAGELIST (identical code to CListView, plate comment) |
| 0x00439af8 | CTreeCtrl_RemoveImageList | C. TVM_GET/SETIMAGELIST (identical code to CTreeView, plate comment) |
| 0x00439b4d | CSliderCtrl_dtor | C. stores vtable 0x45b82c (RTTI CSliderCtrl), DestroyWindow, ~CWnd |
| 0x00439c25 | CImageList_scalar_dtor | C. vtable 0x45b8f4 (RTTI CImageList) |
| 0x00439c90 | CImageList_Detach | C. removes the handle from afxMapHIMAGELIST |
| 0x00439d66 | CImageList_DeleteTempMap | C. afxMapHIMAGELIST->DeleteTemp |
| 0x00439d7c | CImageList_FromHandlePermanent | C. afxMapHIMAGELIST lookup |
| 0x00439d9a | CThreadLocal_AFX_MODULE_THREAD_STATE_CreateObject | C. new AFX_MODULE_THREAD_STATE (0x84) |
| 0x00439df7 | CThreadLocal_AFX_CTL3D_THREAD_CreateObject | C. vtable 0x45bf7c (RTTI _AFX_CTL3D_THREAD); THREAD_LOCAL in MFC |
| 0x00439e64 | _AFX_CTL3D_THREAD_scalar_dtor | C. vtable 0x45bf7c |
| 0x00439e7f | CProcessLocal_AFX_BASE_MODULE_STATE_CreateObject | C. new _AFX_BASE_MODULE_STATE (0x1074) |
| 0x00439ef4 | CException_dtor | C. vtable 0x45c0f4 (RTTI CException); called by derived-class funclets |
| 0x00439f35 | CException_scalar_dtor | C. slot 1 of CException vtable |
| 0x00439f8d | CFileException_scalar_dtor | C. vtable 0x45c0d4 (RTTI CFileException), destroys m_strFileName |
| 0x0043a00c | CSimpleException_scalar_dtor | C. vtable 0x45c3f4 (RTTI CSimpleException) |
| 0x0043a064 | CSimpleException_dtor | C. called by funclets of CResource/CUser/CMemory/CNotSupported dtors |
| 0x0043a0a5 | CResourceException_scalar_dtor | C. vtable 0x45c414 |
| 0x0043a0fd | CUserException_scalar_dtor | C. vtable 0x45c434 |
| 0x0043a179 | CDC_TextOut | C. virtual CDC::TextOut (CDC vtable 0x45c2dc, inherited by CPaintDC 0x45c35c) |
| 0x0043a1f5 | CDC_DrawText | C. virtual CDC::DrawText |
| 0x0043a248 | CDC_Escape | C. virtual CDC::Escape |
| 0x0043a265 | CMemoryException_scalar_dtor | C. vtable 0x45c4e4 |
| 0x0043a2bd | CNotSupportedException_scalar_dtor | C. vtable 0x45c504 |
| 0x0043a315 | CProcessLocal_AFX_PROPPAGEFONTINFO_CreateObject | C. new _AFX_PROPPAGEFONTINFO (0xc) |
| 0x0043a3e0 | _CallMemberFunction1 | C. BuildCatchObject copy-ctor call without the vbase flag |
| 0x0043a3f0 | _CallMemberFunction2 | C. BuildCatchObject call site pushes the extra `1` (vbase copy ctor) |
| 0x0043a759 | _NLG_Notify1 | C. exsup.asm NLG debugger notification (ecx variant) |
| 0x0043a762 | _NLG_Notify | C. exsup.asm NLG notification ([ebp+8] variant) |
| 0x0043a9d0 | _fopen | C. `_fsopen(name, mode, _SH_DENYNO)`; char* caller Log_Printf |
| 0x0043aa90 | _cfltcvt_init | C. fills _cfltcvt_tab[0..5] (slots 0 and 5 = _cfltcvt) |
| 0x0043af50 | type_info_scalar_dtor | C. vtable 0x45c69c (RTTI type_info) |
| 0x0043afc8 | __EH_prolog | C. C++ EH frame prolog helper |
| 0x0043b1f0 | _memcpy | C. callers CString AssignCopy/CopyBeforeWrite/GetBuffer |
| 0x0043b4a0 | __lockexit | C. _lock(_EXIT_LOCK1=0xd) |
| 0x0043b4b0 | __unlockexit | C. _unlock(0xd) |
| 0x0043b4e0 | __chkstk | C. stack probe (_alloca_probe) |
| 0x0043b530 | _wcslen | C. |
| 0x0043b7e0 | _memmove | C. callers cvt.c _shift, CDialogTemplate::SetFont, BuildCatchObject |
| 0x0043bcd0 | _errno | C. &_getptd()->_terrno |
| 0x0043bce0 | __doserrno | C. &_getptd()->_tdoserrno |
| 0x0043ca5d | CallCatchBlock_finally | C. __finally block of CallCatchBlock (scope table 0x45c800): restores frame info and destroys the exception object |
| 0x0043d140 | __unlock | C. LeaveCriticalSection(_locktable[n]) |
| 0x0043d1a0 | __lock_file2 | C. |
| 0x0043d210 | __unlock_file2 | C. |
| 0x0043ebc3 | _ctranexit | C. 87cdisp.asm common exit of _ctrandisp1/2 (clears flag, falls into _cintrinexit) |
| 0x0043ebca | _cintrinexit | C. 87cdisp.asm exit for _cintrindisp1/2 -> _87except |
| 0x0043ed70 | _input | C. scanf engine (called by sscanf) |
| 0x0043fa10 | _hextodec | C. input.c static |
| 0x0043fa50 | _inc | C. input.c static (getc) |
| 0x0043fa80 | _un_inc | C. input.c static (ungetc) |
| 0x0043faa0 | _whiteout | C. input.c static (skip whitespace) |
| 0x00440810 | _filbuf | C. |
| 0x00440910 | _read | C. lock fhandle + _read_lk |
| 0x00441641 | __seh_longjmp_unwind | C. `__seh_longjmp_unwind@4`: _local_unwind2(jmpbuf.Registration, jmpbuf.TryLevel) |
| 0x004418d0 | __CxxUnhandledExceptionFilter | C. msc C++ exception -> terminate(), else chains previous filter |
| 0x00441a60 | _strcpy | C. caller _strdup |
| 0x00441d40 | __allmul | C. 64-bit multiply helper (used by _input for %I64) |
| 0x00441d80 | terminate | C. the only `terminate` |
| 0x00441df5 | terminate_finally | C. `jmp _abort`: __finally of terminate (scope table 0x45cc20) |
| 0x00441e6d | _inconsistency_finally | C. `jmp terminate`: __finally of _inconsistency (scope table 0x45cc38) |
| 0x004434c0 | _fptrap | C. _amsg_exit(_RT_FLOAT) |
| 0x00443a90 | __ld12tod | C. _ld12cvt with DoubleFormat {1024,-1023,53,11,64,1023} |
| 0x00443ab0 | __ld12tof | C. _ld12cvt with FloatFormat {128,-127,24,8,32,127} |
| 0x00443ad0 | __atodbl | C. calls __ld12tod |
| 0x00443b10 | __atoflt | C. calls __ld12tof |
| 0x00444020 | _mbtowc | C. _SETLOCALE_LOCK wrapper |
| 0x00444080 | _mbtowc_lk | C. |
| 0x00444190 | isspace | C. function form of isspace (_isctype(c,_SPACE)) |
| 0x004441c0 | _ungetc_lk | C. |
| 0x00445680 | _strncnt_crtLCMapStringA | C. static strncnt in a_map.c (copy used by __crtLCMapStringA) |
| 0x004459a0 | _abort | C. the CRT abort |
| 0x004461a0 | _free_lc_time | C. inittime.c, frees 43 lc_time strings |
| 0x00446840 | _get_lc_lconv | C. initmon.c, LOCALE_SINTLSYMBOL..SNEGATIVESIGN etc. |
| 0x00446990 | fix_grouping_monetary | C. initmon.c copy of static fix_grouping (0x446700 is the initnum.c copy) |
| 0x004469e0 | _free_lc_lconv | C. initmon.c |
| 0x004484f0 | _matherr | C. default _matherr (returns 0), called by _87except |
| 0x00448580 | __set_statfp | C. raises the given FP status bits; called by _handle_exc |
| 0x00448780 | __frnd | C. fld/frndint/fstp |
| 0x004493e0 | _strncnt_crtCompareStringA | C. static strncnt in a_cmp.c (copy used by __crtCompareStringA) |
| 0x00449750 | Ctl3d_GetSavedWndProcProp | B. CTL3D: GetProp(hwnd, "C3dNew" atom) = saved original wndproc |
| 0x00449770 | Ctl3d_GetDefWndProc | B. CTL3D: saved proc, else per-class default proc (stored back as prop) |
| 0x004497c0 | Ctl3d_SubclassWindow | B. CTL3D: SetWindowLong(GWL_WNDPROC) unless any C3d prop exists; saves old proc |
| 0x00449960 | Ctl3d_InstallCallWndProcHook | B. CTL3D: SetWindowsHookEx(WH_CALLWNDPROC) for current thread |
| 0x004499a0 | Ctl3d_CleanupSubclass | B. CTL3D: call original proc then RemoveProp (NCDESTROY path) |
| 0x00449a00 | Ctl3d_DeleteObjectNull | B. CTL3D: DeleteObject(*p); *p=0 |
| 0x00449a20 | Ctl3d_DeleteGdiObjects | B. CTL3D: frees cached brushes/bitmap |
| 0x00449a50 | Ctl3d_PatFill | B. CTL3D: PatBlt PATCOPY of a RECT |
| 0x00449a80 | Ctl3d_DrawRect3d | B. CTL3D: draws 3D edges with two system colours via ExtTextOut |
| 0x00449bc0 | Ctl3d_DrawInsetRect3d | B. CTL3D: double inset border |
| 0x00449c70 | Ctl3d_GetTextExtentNoMnemonic | B. CTL3D: text extent after removing & and DBCS menu markers |
| 0x00449d20 | Ctl3dRegister | C. MFC Enable3dControlsStatic stores it as m_pfnRegister |
| 0x00449d90 | Ctl3dUnregister | C. m_pfnUnregister |
| 0x00449e50 | Ctl3dAutoSubclass | C. m_pfnAutoSubclass; = AutoSubclassEx(h, 0) |
| 0x00449e60 | Ctl3dAutoSubclassEx | B (API name inferred). per-thread WH_CBT hook table (max 0x80) |
| 0x00449fa0 | Ctl3dIsAutoSubclass | B (API name inferred). is the current thread in the hook table |
| 0x00449fd0 | Ctl3dUnAutoSubclass | C. m_pfnUnAutoSubclass |
| 0x0044a0a0 | Ctl3dUnsubclassCtl | B (API name inferred). restores original wndproc on hwnd and its children, recursively |
| 0x0044a240 | Ctl3d_SubclassCtlIfAllowed | B. subclass one child unless it opts out (msgs 0x1944/0x1943) |
| 0x0044a2f0 | Ctl3d_SubclassDlgChildren | B. subclass dialog children and grandchildren (from Ctl3d_DlgSubclassProc) |
| 0x0044a3e0 | Ctl3dCtlColorEx | B (API name inferred). WM_CTLCOLOR* -> sets colours, returns 3D brush |
| 0x0044a4a0 | Ctl3d_DlgFramePaint | B. call def proc then paint the 3D dialog frame (from Ctl3d_DlgSubclassProc) |
| 0x0044a750 | Ctl3dWinIniChange | C. m_pfnWinIniChange; rereads kanjimenu/hangeulmenu |
| 0x0044a7f0 | Ctl3d_Init3dDialogs | B. first-register init: colour depth check, C3d* atoms, class default procs |
| 0x0044a9e0 | Ctl3d_End3dDialogs | B. last-unregister cleanup: GDI objects, atoms |

</details>

<details><summary>Functions: CTL3D and MFC (0x44AAC0–0x4524A1) (108)</summary>

| Address | Name | What it is |
|---|---|---|
| 0x0044aac0 | Ctl3d_InitColorsAndBrushes | CTL3D: cache 8 system colours, create face/shadow/highlight brushes and the remapped check/radio bitmap (res 0x67c7). Returns early if the colours are unchanged and not forced. |
| 0x0044ac50 | Ctl3d_DlgSubclassProc | CTL3D dialog subclass wndproc: WM_INITDIALOG auto-subclass, WM_CTLCOLOR* handling, NC paint |
| 0x0044af80 | Ctl3d_IsSubclassedDlg | CTL3D: TRUE if the hwnd carries CTL3D subclass data and lacks DS_3DLOOK on Win4 |
| 0x0044afc0 | Ctl3d_CbtHookProc | CTL3D per-thread WH_CBT hook: on HCBT_CREATEWND it subclasses dialogs (atom 0x8002) and the controls inside 3D dialogs |
| 0x0044b170 | Ctl3d_FCanSubclassButton | CTL3D FCanSubclass for Button table entry (pre-Win4 only, button styles < 10) |
| 0x0044b1a0 | Ctl3d_FCanSubclassEdit | CTL3D FCanSubclass for Edit entry (rejects children of a ComboBox on Win4). Same body as 0x44b1f0 (plate comment). |
| 0x0044b1f0 | Ctl3d_FCanSubclassListBox | CTL3D FCanSubclass for ListBox entry (same body as Edit's) |
| 0x0044b3a0 | Ctl3d_DoSubclassCtl | CTL3D: match the class name against the table, ask its FCanSubclass, install the 3D wndproc |
| 0x0044b490 | Ctl3d_InvalidateCtlFrame | CTL3D: invalidate the 3D border area around a control in its parent (WM_SHOWWINDOW/WM_WINDOWPOSCHANGED) |
| 0x0044b550 | Ctl3d_ValidateCtlFrame | CTL3D: validate that same border rect in the parent |
| 0x0044b5c0 | Ctl3d_DrawPushButton | CTL3D: draw a 3D push button (frame, face, text, focus rect) |
| 0x0044b900 | Ctl3d_BtnPaint | CTL3D: paint any button style (push, check/radio bitmap, group box) |
| 0x0044be50 | Ctl3d_BtnWndProc3d | CTL3D Button subclass wndproc (table entry 0x45d3dc) |
| 0x0044c130 | Ctl3d_PaintCtlFrame | CTL3D: draw the sunken 3D frame of an edit/listbox/combo onto its parent DC (combo-aware) |
| 0x0044c470 | Ctl3d_ListComboWndProcCommon | CTL3D common subclass proc for ListBox/ComboBox/ComboLBox (ct index argument; answers 1002) |
| 0x0044c5f0 | Ctl3d_EditWndProcCommon | CTL3D common subclass proc for Edit (answers 1001) |
| 0x0044c8a0 | Ctl3d_StaticDrawText | CTL3D: draw a static control's text with SS_* alignment flags |
| 0x0044c980 | Ctl3d_StaticPaint | CTL3D: paint static controls (text, etched frames/rects) |
| 0x0044cc70 | Ctl3d_LibMain | CTL3D library init (process attach): DisableThreadLibraryCalls, version, metrics |
| 0x0044cd40 | Ctl3d_LoadUIBitmap | Load a DIB resource and remap its palette to the current system colours (LoadUIBitmap) |
| 0x0044cef0 | AfxInitialize | MFC AfxInitialize(bDLL, ver): sets module m_bDLL, `_setmbcp(_MB_CP_ANSI)` for EXEs |
| 0x0044cf12 | _AFX_TERM_APP_STATE_ctor | ctor of appmodul.cpp static _afxTermAppState (calls AfxInitialize(FALSE)). Its dtor is unnamed code at 0x44cf25 that calls AfxTermLocalData(NULL,TRUE). |
| 0x0044cf39 | _afxTermAppState_construct | dynamic initializer: constructs _afxTermAppState (0x5a537c) |
| 0x0044cf43 | _afxTermAppState_registerAtexit | dynamic initializer: atexit(destroy _afxTermAppState) |
| 0x0044d0ab | CSimpleException_GetErrorMessage | CSimpleException::GetErrorMessage (shared vtable slot of memory/not-supported/resource/user exceptions) |
| 0x0044d0f0 | AfxThrowMemoryException | throw the static CMemoryException (throw info type ".?AVCMemoryException@@") |
| 0x0044d12c | CTime_ctor_YMDHMS | CTime::CTime(int y,m,d,h,min,s,dst) |
| 0x0044d17d | CTime_ctor_SYSTEMTIME | CTime::CTime(const SYSTEMTIME&, int dst) |
| 0x0044d1d2 | CTime_ctor_FILETIME | CTime::CTime(const FILETIME&, int dst) |
| 0x0044d285 | CPtrArray_scalar_dtor | CPtrArray scalar deleting dtor (vtable 0x45c5ac, GetRuntimeClass returns "CPtrArray") |
| 0x0044d2a3 | CPtrArray_dtor | CPtrArray::~CPtrArray (delete[] m_pData) |
| 0x0044d49e | CPtrList_scalar_dtor | CPtrList scalar deleting dtor (vtable 0x45c60c, "CPtrList") |
| 0x0044d4bc | CPtrList_RemoveAll | CPtrList::RemoveAll |
| 0x0044d4dc | CPtrList_dtor | CPtrList::~CPtrList (vtable proves CPtrList, not CObList) |
| 0x0044d613 | CMapPtrToPtr_scalar_dtor | CMapPtrToPtr scalar deleting dtor (vtable 0x45c554, "CMapPtrToPtr") |
| 0x0044d631 | CMapPtrToPtr_InitHashTable | CMapPtrToPtr::InitHashTable |
| 0x0044d680 | CMapPtrToPtr_RemoveAll | CMapPtrToPtr::RemoveAll |
| 0x0044d6af | CMapPtrToPtr_dtor | CMapPtrToPtr::~CMapPtrToPtr (stores vtable 0x45c554) |
| 0x0044d6fc | CMapPtrToPtr_NewAssoc | CMapPtrToPtr::NewAssoc |
| 0x0044d764 | CMapPtrToPtr_GetAssocAt | CMapPtrToPtr::GetAssocAt (key>>4 hash) |
| 0x0044d7cd | CMapPtrToPtr_operator_index | CMapPtrToPtr::operator[] (used by CHandleMap) |
| 0x0044d8da | CMapStringToPtr_InitHashTable | CMapStringToPtr::InitHashTable (only string map in the binary, used by CToolTipCtrl) |
| 0x0044d929 | CMapStringToPtr_NewAssoc | CMapStringToPtr::NewAssoc |
| 0x0044d98b | CMapStringToPtr_GetAssocAt | CMapStringToPtr::GetAssocAt (string hash, _mbscmp) |
| 0x0044da3b | CMapStringToPtr_operator_index | CMapStringToPtr::operator[] |
| 0x0044dae7 | CDialog_PreTranslateMessage | CDialog::PreTranslateMessage |
| 0x0044db8c | CDialog_OnCmdMsg | CDialog::OnCmdMsg |
| 0x0044dc24 | CDialog_ctor_default | CDialog::CDialog() |
| 0x0044dc80 | CDialog_scalar_dtor | CDialog scalar deleting dtor (vtable 0x45ab04) |
| 0x0044df5f | CDialog_ctor_UINT_CWnd | CDialog::CDialog(UINT nIDTemplate, CWnd* pParent) |
| 0x0044e045 | CDialog_DoModal | CDialog::DoModal |
| 0x0044e31d | CDialog_OnInitDialog | CDialog::OnInitDialog |
| 0x0044e437 | CDialog_OnCtlColor | CDialog::OnCtlColor (calls OnGrayCtlColor) |
| 0x0044e44b | CDialog_OnCommandHelp | CDialog::OnCommandHelp (WM_COMMANDHELP: app->WinHelp(HID_BASE_RESOURCE+m_nIDHelp)) |
| 0x0044e4b8 | CWnd_wndTop_construct | dynamic init: CWnd::wndTop = CWnd(HWND_TOP) at 0x5a4af0 |
| 0x0044e4c5 | CWnd_wndTop_registerAtexit | atexit(destroy wndTop) |
| 0x0044e4f8 | CWnd_wndBottom_construct | dynamic init: CWnd::wndBottom = CWnd(HWND_BOTTOM) at 0x5a4bb0 |
| 0x0044e505 | CWnd_wndBottom_registerAtexit | atexit(destroy wndBottom) |
| 0x0044e538 | CWnd_wndTopMost_construct | dynamic init: CWnd::wndTopMost = CWnd(HWND_TOPMOST) at 0x5a4b30 |
| 0x0044e545 | CWnd_wndTopMost_registerAtexit | atexit(destroy wndTopMost) |
| 0x0044e578 | CWnd_wndNoTopMost_construct | dynamic init: CWnd::wndNoTopMost = CWnd(HWND_NOTOPMOST) at 0x5a4b70 |
| 0x0044e585 | CWnd_wndNoTopMost_registerAtexit | atexit(destroy wndNoTopMost) |
| 0x0044e5ae | CWnd_ctor_default | CWnd::CWnd() |
| 0x0044e60a | CWnd_scalar_dtor | CWnd scalar deleting dtor |
| 0x0044e628 | CWnd_ctor_HWND | CWnd::CWnd(HWND) (private ctor used for wndTop etc.) |
| 0x0044e91a | CWnd_DeleteTempMap | CWnd::DeleteTempMap (afxMapHWND) |
| 0x0044e9c1 | CWnd_FromHandle | CWnd::FromHandle |
| 0x0044e9eb | CWnd_FromHandlePermanent | CWnd::FromHandlePermanent |
| 0x0044ea09 | CWnd_Attach | CWnd::Attach |
| 0x0044ea47 | CWnd_Detach | CWnd::Detach |
| 0x0044eab0 | AfxGetAfxWndProc | returns &AfxWndProc (code at 0x44ea80, not yet a function) |
| 0x0044f070 | CWnd_PreCreateWindow | CWnd::PreCreateWindow (default class "AfxWnd42s") |
| 0x0044f0aa | CWnd_Create | CWnd::Create(class, title, style\|WS_CHILD, rect, parent, nID, ctx) |
| 0x0044f360 | CWnd_PreTranslateMessage | CWnd::PreTranslateMessage |
| 0x0044f4d6 | CWnd_OnCharToItem | CWnd::OnCharToItem (message map entry WM_CHARTOITEM 0x2F) |
| 0x0044f506 | CWnd_OnVKeyToItem | CWnd::OnVKeyToItem (message map entry WM_VKEYTOITEM 0x2E) |
| 0x0044f6cb | CWnd_OnNTCtlColor | CWnd::OnNTCtlColor: WM_CTLCOLORxxx is passed on as a WM_CTLCOLOR call to WindowProc |
| 0x0044f70a | CWnd_WinHelp | CWnd::WinHelp |
| 0x0044ffbe | CWnd_OnCommand | CWnd::OnCommand |
| 0x00450068 | CWnd_OnNotify | CWnd::OnNotify |
| 0x00450513 | CWnd_OnChildNotify | CWnd::OnChildNotify |
| 0x004508e8 | CWnd_OnHScroll | CWnd::OnHScroll (message map entry WM_HSCROLL) |
| 0x00450909 | CWnd_OnVScroll | CWnd::OnVScroll (message map entry WM_VSCROLL) |
| 0x0045097a | CWnd_OnCtlColor | CWnd::OnCtlColor |
| 0x00450cdb | CWnd_ExecuteDlgInit_ByName | CWnd::ExecuteDlgInit(LPCTSTR) - loads RT_DLGINIT |
| 0x00450d2c | CWnd_ExecuteDlgInit_ByData | CWnd::ExecuteDlgInit(LPVOID) - applies the data |
| 0x00450f66 | CWnd_ContinueModal | CWnd::ContinueModal (m_nFlags & WF_CONTINUEMODAL) |
| 0x004511a0 | CWnd_GetDlgItem | CWnd::GetDlgItem(int) -> CWnd* |
| 0x004511c6 | CWnd_GetDlgItem_HWND | CWnd::GetDlgItem(int, HWND*) |
| 0x00451225 | CWnd_GetStyle | CWnd::GetStyle. It reads GWL_STYLE (-16); Function ID matches it as GetExStyle. |
| 0x00451241 | CWnd_GetExStyle | CWnd::GetExStyle (GWL_EXSTYLE) |
| 0x0045138b | CWnd_SetFocus | CWnd::SetFocus |
| 0x004513ae | CWnd_AttachControlSite_HandleMap | CWnd::AttachControlSite(CHandleMap*) |
| 0x004513de | CWnd_AttachControlSite_Wnd | CWnd::AttachControlSite(CWnd* pWndParent) |
| 0x0045148a | CCmdTarget_scalar_dtor | CCmdTarget scalar deleting dtor (vtable 0x45b144, "CCmdTarget") |
| 0x004514fa | CCmdTarget_OnCmdMsg | CCmdTarget::OnCmdMsg |
| 0x00451a01 | global_operator_new | global ::operator new (MFC afxmem.cpp, with new-handler loop). Distinct from CNoTrackObject::operator new at 0x457eb2. |
| 0x00451a41 | global_operator_delete | global ::operator delete (free). Distinct from CNoTrackObject::operator delete at 0x457ed0. |
| 0x00451a54 | CWinApp_Run | CWinApp::Run |
| 0x00451a76 | CWinApp_WinHelp | CWinApp::WinHelp |
| 0x00451b3d | CWinApp_OnIdle | CWinApp::OnIdle |
| 0x00451cdf | CWinThread_Run | CWinThread::Run |
| 0x00451dc2 | CWinThread_OnIdle | CWinThread::OnIdle |
| 0x0045232f | CMenu_DeleteTempMap | CMenu::DeleteTempMap (afxMapHMENU) |
| 0x004523d6 | CMenu_FromHandle | CMenu::FromHandle |
| 0x004523ee | CMenu_FromHandlePermanent | CMenu::FromHandlePermanent |
| 0x0045243e | CMenu_Detach | CMenu::Detach (afxMapHMENU, so not CGdiObject) |
| 0x004524a1 | AfxDynamicDownCast | AfxDynamicDownCast(CRuntimeClass*, CObject*) |

</details>

<details><summary>Functions: MFC (0x4524BF–0x4588C1) (108)</summary>

| Address | Name | What it is |
|---|---|---|
| 0x004524bf | CRuntimeClass_CreateObject | CRuntimeClass::CreateObject (TRY/CATCH around m_pfnCreateObject) |
| 0x0045253a | CFile_ctor | CFile::CFile() |
| 0x004525a8 | CFile_scalar_dtor | CFile scalar deleting dtor (vtable 0x45c074) |
| 0x004525c6 | CFile_ctor_hFile | CFile::CFile(int hFile) |
| 0x00452cfe | AfxGetEmptyString | returns &_afxPchNil (empty CString) |
| 0x00452d14 | CString_copy_ctor | CString::CString(const CString&) |
| 0x00452d86 | CString_Release | CString::Release() (member) |
| 0x00452dbf | CString_Release_pData | static CString::Release(CStringData*) |
| 0x00452e93 | CString_ctor_lpsz | CString::CString(LPCTSTR) (also handles MAKEINTRESOURCE) |
| 0x00452fe3 | CException_ctor | CException::CException() |
| 0x0045303a | CException_ctor_bAutoDelete | CException::CException(BOOL bAutoDelete) |
| 0x004531b4 | CDialogTemplate_dtor | CDialogTemplate::~CDialogTemplate (GlobalFree m_hTemplate) |
| 0x004531c2 | CDialogTemplate_Detach | CDialogTemplate::Detach (returns m_hTemplate, NULLs it) |
| 0x0045352c | AfxLockTempMaps | ++AfxGetModuleThreadState()->m_nTempMapLock |
| 0x0045369a | CHandleMap_FromHandle | CHandleMap::FromHandle (permanent/temp map lookup, creates temp object) |
| 0x004542b4 | CDC_scalar_dtor | CDC scalar deleting dtor (vtable 0x45c2dc) |
| 0x004542d2 | CDC_DeleteTempMap | CDC::DeleteTempMap (afxMapHDC) |
| 0x00454379 | CDC_FromHandle | CDC::FromHandle(HDC) (afxMapHDC) |
| 0x00454391 | CDC_Attach | CDC::Attach(HDC) |
| 0x004543cd | CDC_Detach | CDC::Detach() |
| 0x0045454b | CDC_SelectObject_CPen | CDC::SelectObject(CPen*) |
| 0x004545a5 | CDC_SelectObject_CFont | virtual CDC::SelectObject(CFont*) (CDC vtbl +0x30, also CPaintDC) |
| 0x004545ff | CDC_SetBkColor | virtual CDC::SetBkColor (vtbl +0x34) |
| 0x0045463b | CDC_SetTextColor | virtual CDC::SetTextColor (vtbl +0x38) |
| 0x00454677 | CDC_SetMapMode | virtual CDC::SetMapMode (vtbl +0x3c) |
| 0x004546b3 | CDC_SetViewportOrg | virtual CDC::SetViewportOrg (SetViewportOrgEx) |
| 0x00454704 | CDC_OffsetViewportOrg | virtual CDC::OffsetViewportOrg (OffsetViewportOrgEx) |
| 0x00454755 | CDC_SetViewportExt | virtual CDC::SetViewportExt (SetViewportExtEx) |
| 0x004547a6 | CDC_ScaleViewportExt | virtual CDC::ScaleViewportExt (ScaleViewportExtEx) |
| 0x00454803 | CDC_SetWindowExt | virtual CDC::SetWindowExt (SetWindowExtEx) |
| 0x00454854 | CDC_ScaleWindowExt | virtual CDC::ScaleWindowExt (ScaleWindowExtEx) |
| 0x00454937 | CPaintDC_scalar_dtor | CPaintDC scalar deleting dtor (vtable 0x45c35c) |
| 0x004549b2 | CGdiObject_DeleteTempMap | CGdiObject::DeleteTempMap (afxMapHGDIOBJ) |
| 0x00454a59 | CGdiObject_FromHandle | CGdiObject::FromHandle(HGDIOBJ) |
| 0x00454a71 | CGdiObject_Attach | CGdiObject::Attach(HGDIOBJ) (afxMapHGDIOBJ) |
| 0x00454aa3 | CGdiObject_Detach | CGdiObject::Detach() (afxMapHGDIOBJ) |
| 0x0045503e | AfxMessageBox_Text | AfxMessageBox(LPCTSTR, UINT, UINT) |
| 0x0045505d | AfxMessageBox_ID | AfxMessageBox(UINT nIDPrompt, UINT, UINT) |
| 0x00455232 | AfxFormatStrings_ID | AfxFormatStrings(CString&, UINT nIDS, LPCTSTR const*, int) |
| 0x0045526e | AfxFormatStrings_Str | AfxFormatStrings(CString&, LPCTSTR lpszFormat, LPCTSTR const*, int) |
| 0x0045545d | CPropertyPage_scalar_dtor | CPropertyPage scalar deleting dtor |
| 0x0045547b | CPropertyPage_CommonConstruct | CPropertyPage::CommonConstruct(LPCTSTR, UINT) |
| 0x0045583f | CPropertyPage_OnNotify | CPropertyPage::OnNotify (PSN_* dispatch); in the CPropertyPage vtable and two game-derived page vtables |
| 0x00455984 | CPropertyPage_OnCtlColor | CPropertyPage::OnCtlColor (message-map entry) |
| 0x00455abc | CPropertySheet_scalar_dtor | CPropertySheet scalar deleting dtor |
| 0x00455b74 | CPropertySheet_CommonConstruct | CPropertySheet::CommonConstruct(CWnd*, UINT) |
| 0x00455cb1 | CPropertySheet_OnCmdMsg | CPropertySheet::OnCmdMsg |
| 0x00455d77 | CPropertySheet_ContinueModal | CPropertySheet::ContinueModal (CWnd::ContinueModal && PSM_GETCURRENTPAGEHWND) |
| 0x00455d9c | CPropertySheet_DoModal | CPropertySheet::DoModal |
| 0x00456085 | CPropertySheet_OnInitDialog | CPropertySheet::OnInitDialog |
| 0x004562a7 | CPropertySheet_OnCommand | CPropertySheet::OnCommand |
| 0x00456339 | CPropertySheet_OnCtlColor | CPropertySheet::OnCtlColor (message-map entry) |
| 0x004563b4 | CStatic_dtor | CStatic::~CStatic (vtable 0x45b9dc, DestroyWindow) |
| 0x00456401 | CButton_dtor | CButton::~CButton (vtable 0x45baa4) |
| 0x00456451 | CButton_OnChildNotify | CButton::OnChildNotify (WM_DRAWITEM -> DrawItem); CButton vtbl +0xb0 |
| 0x00456482 | CComboBox_dtor | CComboBox::~CComboBox (vtable 0x45bb6c) |
| 0x004564dd | CComboBox_OnChildNotify | CComboBox::OnChildNotify (DRAW/MEASURE/COMPARE/DELETEITEM); CComboBox vtbl +0xb0 |
| 0x00456549 | CEdit_dtor | CEdit::~CEdit (vtable 0x45bc44) |
| 0x004565b3 | AfxOleGetUserCtrl | returns AfxGetModuleState()->m_bUserCtrl (used by CWinApp::Run) |
| 0x004565c6 | Mfc_GdiBrushTermObj_init_ctor | BEHAVIOUR: empty constructor half of a `$E` initializer for a static terminator object (see next row) |
| 0x004565c7 | Mfc_GdiBrushTermObj_init_atexit | BEHAVIOUR: atexit-registers a thunk that does AfxDeleteObject on the HBRUSH global at 0x5a5120. That global has no other xrefs; probably the wingdix.cpp _afxHalftoneBrush terminator (plate comment) |
| 0x004565e6 | CWnd_CreateObject | CWnd::CreateObject (new 0x3c, CWnd ctor) |
| 0x00456741 | CImageList_CreateObject | CImageList::CreateObject |
| 0x004567bc | _AFX_WIN_STATE_ctor | _AFX_WIN_STATE::_AFX_WIN_STATE (CNoTrackObject vtbl then 0x45bdd4) |
| 0x0045680c | _AFX_WIN_STATE_scalar_dtor | _AFX_WIN_STATE scalar deleting dtor |
| 0x00456936 | CWinApp_scalar_dtor | CWinApp scalar deleting dtor |
| 0x00456abb | CCommandLineInfo_scalar_dtor | CCommandLineInfo scalar deleting dtor |
| 0x00456ee6 | _afxWinState_init_ctor | `$E` init (ctor half) of PROCESS_LOCAL(_AFX_WIN_STATE,_afxWinState) @0x5a4bf0 |
| 0x00456ee7 | _afxWinState_init_atexit | atexit-registers ~CProcessLocalObject for _afxWinState |
| 0x00456f2c | CWinThread_scalar_dtor | CWinThread scalar deleting dtor |
| 0x00456fa4 | CWinThread_CommonConstruct | CWinThread::CommonConstruct |
| 0x00456fdc | CMenu_CreateObject | CMenu::CreateObject (CRuntimeClass 0x45bf88 -> "CMenu") |
| 0x004570b2 | _AFX_THREAD_STATE_scalar_dtor | _AFX_THREAD_STATE scalar deleting dtor |
| 0x00457184 | _afxThreadState_init_ctor | `$E` init (ctor half) of THREAD_LOCAL(_AFX_THREAD_STATE,_afxThreadState) @0x5a4bf4 |
| 0x00457185 | _afxThreadState_init_atexit | atexit-registers ~CThreadLocalObject for _afxThreadState |
| 0x00457284 | AFX_MODULE_STATE_scalar_dtor | AFX_MODULE_STATE scalar deleting dtor |
| 0x0045729f | CThreadLocal_AFX_MODULE_THREAD_STATE_dtor | CThreadLocal<AFX_MODULE_THREAD_STATE>::~CThreadLocal (AFX_MODULE_STATE::m_thread at +0x1070); plate comment |
| 0x00457456 | AFX_MODULE_THREAD_STATE_scalar_dtor | AFX_MODULE_THREAD_STATE scalar deleting dtor |
| 0x00457688 | _afxBaseModuleState_init_ctor | `$E` init (ctor half) of PROCESS_LOCAL(_AFX_BASE_MODULE_STATE,_afxBaseModuleState) @0x5a4bf8 |
| 0x00457689 | _afxBaseModuleState_init_atexit | atexit-registers ~CProcessLocalObject for _afxBaseModuleState |
| 0x004576ce | _AFX_BASE_MODULE_STATE_scalar_dtor | _AFX_BASE_MODULE_STATE scalar deleting dtor (vtable 0x45bff8, runs ~AFX_MODULE_STATE) |
| 0x004579f8 | _simpleResourceException_init_ctor | constructs static CResourceException _simpleResourceException(FALSE, 0xF022) @0x5a4c98 |
| 0x00457a78 | _simpleResourceException_init_atexit | atexit-registers its destructor |
| 0x00457ad7 | _simpleUserException_init_ctor | constructs static CUserException _simpleUserException(FALSE, 0xF024) @0x5a4c00 |
| 0x00457b57 | _simpleUserException_init_atexit | atexit-registers its destructor |
| 0x00457ba6 | CDC_CreateObject | CDC::CreateObject (CRuntimeClass 0x45c1d0 -> "CDC") |
| 0x00457c0f | CGdiObject_CreateObject | CGdiObject::CreateObject (CRuntimeClass 0x45c230 -> "CGdiObject") |
| 0x00457ca4 | _simpleMemoryException_init_ctor | constructs static CMemoryException _simpleMemoryException(FALSE, 0xF023) @0x5a4d30 |
| 0x00457d24 | _simpleMemoryException_init_atexit | atexit-registers its destructor |
| 0x00457d83 | _simpleNotSupportedException_init_ctor | constructs static CNotSupportedException _simpleNotSupportedException(FALSE, 0xF021) @0x5a4dc8 |
| 0x00457e03 | _simpleNotSupportedException_init_atexit | atexit-registers its destructor |
| 0x00457eb2 | CNoTrackObject_operator_new | CNoTrackObject::operator new (LocalAlloc LPTR) |
| 0x00457ed0 | CNoTrackObject_operator_delete | CNoTrackObject::operator delete (LocalFree) |
| 0x004581a9 | CThreadData_scalar_dtor | CThreadData scalar deleting dtor (vtable 0x45c524, set in CThreadSlotData::SetValue) |
| 0x004581f8 | CThreadSlotData_DeleteValues_pData | CThreadSlotData::DeleteValues(CThreadData*, HINSTANCE) |
| 0x004582b0 | CThreadSlotData_DeleteValues_hInst | CThreadSlotData::DeleteValues(HINSTANCE, BOOL bAll) |
| 0x00458309 | CThreadLocalObject_GetData | CThreadLocalObject::GetData(pfnCreateObject) |
| 0x004583ae | CProcessLocalObject_GetData | CProcessLocalObject::GetData(pfnCreateObject) |
| 0x00458450 | AfxTermLocalData | AfxTermLocalData(HINSTANCE, BOOL) -> _afxThreadData->DeleteValues |
| 0x00458536 | _afxCtl3dThread_init_ctor | `$E` init (ctor half) of THREAD_LOCAL(_AFX_CTL3D_THREAD,_afxCtl3dThread) @0x5a4ea4 |
| 0x00458537 | _afxCtl3dThread_init_atexit | atexit-registers ~CThreadLocalObject for _afxCtl3dThread |
| 0x00458586 | _afxCtl3dState_init_ctor | `$E` init (ctor half) of PROCESS_LOCAL(_AFX_CTL3D_STATE,_afxCtl3dState) @0x5a4ea0 |
| 0x00458587 | _afxCtl3dState_init_atexit | atexit-registers ~CProcessLocalObject for _afxCtl3dState |
| 0x004585d6 | afxData_init_ctor | `$E` init: AUX_DATA::AUX_DATA(&afxData @0x5a4ea8) |
| 0x004585e0 | afxData_init_atexit | atexit-registers ~AUX_DATA for afxData |
| 0x004587c5 | _afxPropPageFontInfo_init_ctor | `$E` init (ctor half) of PROCESS_LOCAL(_AFX_PROPPAGEFONTINFO,_afxPropPageFontInfo) @0x5a511c |
| 0x004587c6 | _afxPropPageFontInfo_init_atexit | atexit-registers ~CProcessLocalObject for _afxPropPageFontInfo |
| 0x004588c1 | _AFX_PROPPAGEFONTINFO_scalar_dtor | _AFX_PROPPAGEFONTINFO scalar deleting dtor (GlobalFree font name) |

</details>

<details><summary>Helpers, funclets and initialisers (200)</summary>

| Address | Name | Kind | What it is |
|---|---|---|---|
| 00439a39 | CNoTrackObject_scalar_dtor | function | Deleting destructor in CNoTrackObject vtable slot 0 (0x45b024). It resets the vptr and calls CNoTrackObject::operator delete. |
| 00439ffd | CFile_SetFilePath | function | CFile vtable slot 9: `m_strFileName = lpszNewName`. |
| 0043a155 | CDC_PtVisible | function | `::PtVisible(m_hDC,x,y)` (CDC/CPaintDC vtables, slot 23). |
| 0043a169 | CDC_RectVisible | function | `::RectVisible(m_hDC,lpRect)`. |
| 0043a212 | CDC_GrayString | function | `::GrayStringA(m_hDC, pBrush->m_hObject, ...)`, ret 0x20. |
| 0043a510 | CatchGuardHandler | function | C++ EH (trnsctrl.cpp): cld; forwards to __InternalCxxFrameHandler. |
| 0043a6ac | __unwind_handler | function | exsup.asm unwind handler used by __global_unwind2/__local_unwind2. |
| 0043aa80 | __fpclear | function | Empty `ret`. It is the value of the `_FPmtinit`/`_FPmtterm` pointers (0x4671f4/f8). |
| 0043cd2a | DestructExceptionObject_SEHFilter | function | `__except` filter funclet of DestructExceptionObject (scope table 0x45c820). |
| 0043cd32 | DestructExceptionObject_SEHHandler | function | `__except` block: restores ESP and calls terminate(). |
| 0043e5a0 | __forcdecpt | function | CRT cvt.c `_forcdecpt` (`_cfltcvt_tab[3]`). |
| 0043e610 | __cropzeros | function | CRT `_cropzeros` (`_cfltcvt_tab[1]`). |
| 0043e670 | __positive | function | CRT `_positive` (`_cfltcvt_tab[4]`). |
| 0043e690 | __fassign | function | CRT `_fassign` (_atodbl/_atoflt) (`_cfltcvt_tab[2]`). |
| 0043ea30 | __cfltcvt | function | CRT `_cfltcvt`, dispatches to _cftoe/_cftof/_cftog (`_cfltcvt_tab[0,5]`). |
| 0044157c | __except_handler3_signature | data | `char[8]` "VC20XC00" signature in front of _except_handler3. |
| 00441584 | __except_handler3 | function | CRT SEH frame handler (exsup3). Referenced by every SEH frame. |
| 004418a0 | __CxxSetUnhandledExceptionFilter_init | function | `$E` initializer from the __xc table (0x46100c): jmp to the next function. |
| 004418b0 | __CxxSetUnhandledExceptionFilter | function | Registers the restore function with atexit and installs __CxxUnhandledExceptionFilter. Reached only as a jump target. |
| 00441930 | __CxxRestoreUnhandledExceptionFilter | function | Puts back the previous unhandled-exception filter at exit. |
| 004425f0 | ___init_dummy | function | `return 0`. LC_ALL init entry of the __lc_category table (0x468670). |
| 00445d70 | ___init_time | function | CRT `__init_time` (LC_TIME init, table 0x4686ac). |
| 00446750 | ___init_monetary | function | Real entry of CRT `__init_monetary` (LC_MONETARY init, table 0x468694). Its body includes range 0x4467ea-0x44683e (the C-locale path). |
| 00446d00 | ___init_collate | function | `return 0`. LC_COLLATE init (table 0x46867c). |
| 004498b0 | Ctl3d_CallWndProcHook | function | CTL3D WH_CALLWNDPROC hook installed by Ctl3d_InstallCallWndProcHook. It unhooks, then sends WM_DLGSUBCLASS (0x11f0) and subclasses the dialog. |
| 0044c820 | Ctl3d_ListWndProc3d | function | CTL3D ListBox/ComboLBox subclass proc (class table 0x45d3fc). Calls Ctl3d_ListWndProcCommon(…,ctList=1). |
| 0044c850 | Ctl3d_ComboWndProc3d | function | CTL3D ComboBox subclass proc. Calls Ctl3d_EditComboWndProcCommon(…,ctCombo=3). |
| 0044cf2f | _afxTermAppState_init | function | `$E` stub: call _afxTermAppState_construct; jmp _afxTermAppState_registerAtexit. |
| 0044cf51 | _afxTermAppState_atexitDtor | function | atexit target: `mov ecx,&_afxTermAppState; jmp _AFX_TERM_APP_STATE_dtor`. |
| 0044daa4 | AfxDlgProc | function | MFC dialog proc: on WM_INITDIALOG it calls DYNAMIC_DOWNCAST(CDialog)->OnInitDialog(). |
| 0044dae1 | CDialog_GetMessageMap | function | Returns &CDialog::messageMap (0x45aa00). |
| 0044df4b | CWnd_SetOccDialogInfo | function | CWnd vtable slot 47: `return FALSE`. |
| 0044df50 | CDialog_SetOccDialogInfo | function | `m_pOccDialogInfo = p; return TRUE`. |
| 0044e223 | CDialog_PreInitDialog | function | Empty virtual (CDialog vtable slot 53). |
| 0044e224 | CDialog_HandleInitDialog | function | WM_INITDIALOG message-map handler: OLE control init, ExecuteDlgInit, OnInitDialog, help-focus fixup. |
| 0044e398 | CDialog_OnOK | function | `if (UpdateData(TRUE)) EndDialog(IDOK)`. |
| 0044e3b1 | CDialog_OnCancel | function | `EndDialog(IDCANCEL)`. |
| 0044e485 | CDialog_OnHelpHitTest | function | `m_nIDHelp ? HID_BASE_RESOURCE+m_nIDHelp : 0` (message-map handler). |
| 0044e498 | nMsgDragList_init | function | `$E` thunk (__xc 0x46102c): jmp to nMsgDragList_construct. |
| 0044e49d | nMsgDragList_construct | function | `nMsgDragList = RegisterWindowMessage("commctrl_DragListMsg")` (stored at 0x5a4bec). |
| 0044e4ae | CWnd_wndTop_init | function | `$E` stub: CWnd_wndTop_construct + CWnd_wndTop_registerAtexit. |
| 0044e4d3 | CWnd_wndTop_atexitDtor | function | Guarded destruction of CWnd::wndTop (flag bit 1 at 0x5a32e8). |
| 0044e4ee | CWnd_wndBottom_init | function | `$E` stub for CWnd::wndBottom. |
| 0044e513 | CWnd_wndBottom_atexitDtor | function | Guarded destruction of CWnd::wndBottom. |
| 0044e52e | CWnd_wndTopMost_init | function | `$E` stub for CWnd::wndTopMost. |
| 0044e553 | CWnd_wndTopMost_atexitDtor | function | Guarded destruction of CWnd::wndTopMost. |
| 0044e56e | CWnd_wndNoTopMost_init | function | `$E` stub for CWnd::wndNoTopMost. |
| 0044e593 | CWnd_wndNoTopMost_atexitDtor | function | Guarded destruction of CWnd::wndNoTopMost. |
| 0044ed04 | _AfxGrayBackgroundWndProc | function | Handles WM_CTLCOLOR* with _afxWinState gray brush, else _AfxActivationWndProc. |
| 0044ed80 | _AfxCbtFilterHook | function | MFC WH_CBT hook: on HCBT_CREATEWND it attaches the CWnd and subclasses it with AfxWndProc, or with the gray/activation wndproc. |
| 0044f2af | CWnd_PostNcDestroy | function | Empty virtual (slot 43). |
| 0044f2b0 | CWnd_OnFinalRelease | function | `m_hWnd ? DestroyWindow() : PostNcDestroy()` (slot 6). |
| 0044f35c | CWnd_GetSuperWndProcAddr | function | `return &m_pfnSuper` (slot 34). |
| 0044f3d6 | CWnd_OnToolHitTest | function | Slot 27: finds the child window under the point and fills TOOLINFO. |
| 0044f7fd | CWnd_GetMessageMap | function | Returns &CWnd::messageMap (0x45ac78). |
| 0044ffa4 | CTestCmdUI_Enable | function | `m_bEnabled=bOn; m_bEnableChanged=TRUE` (vtable 0x45b004). |
| 0044ffb5 | CTestCmdUI_SetCheck | function | Empty (ret 4). |
| 0044ffb8 | CTestCmdUI_SetRadio | function | Empty (ret 4). |
| 0044ffbb | CTestCmdUI_SetText | function | Empty (ret 4). |
| 004503ab | CWnd_GetScrollBarCtrl | function | `return NULL` (slot 28). |
| 00450cd5 | CWnd_CheckAutoCenter | function | `return TRUE` (slot 45). |
| 00450f6d | CWnd_EndModalLoop | function | Sets m_nModalResult, clears WF_CONTINUEMODAL, PostMessage(m_hWnd,0,0,0). |
| 0045111d | CWnd_IsFrameWnd | function | `return FALSE` (slot 46). Not in the todo list: a 3-byte orphan next to range 41. |
| 00451189 | CWnd_OnAmbientProperty | function | Slot 39: forwards to m_pCtrlCont. |
| 00451710 | DispatchCmdMsg_switchTable | switch table | `pointer[13]`: jump targets of the nSig switch in DispatchCmdMsg. |
| 00451744 | DispatchCmdMsg_switchIndexTable | switch table | `byte[46]`: index table (sig-2 → case) for the same switch. |
| 00451772 | CCmdTarget_IsInvokeAllowed | function | `return TRUE` (slot 7). |
| 0045177a | CCmdTarget_GetDispatchIID | function | `return FALSE` (slot 8). |
| 0045177f | CCmdTarget_GetTypeInfoCount | function | `return 0` (slot 9). |
| 00451782 | CCmdTarget_GetTypeLibCache | function | `return NULL` (slot 10). |
| 00451785 | CCmdTarget_GetTypeLib | function | `return TYPE_E_CANTLOADLIBRARY` (slot 11). |
| 004517b3 | CCmdTarget_GetMessageMap | function | &CCmdTarget::messageMap (slot 12). |
| 004517b9 | CCmdTarget_GetDispatchMap | function | Slot 14. |
| 004517bf | CCmdTarget_GetEventSinkMap | function | Slot 17. |
| 004517c5 | CCmdTarget_GetInterfaceMap | function | Slot 16. |
| 004517fd | CCmdTarget_OnCreateAggregates | function | `return TRUE` (slot 18). |
| 00451803 | CCmdTarget_GetInterfaceHook | function | `return NULL` (slot 19). |
| 00451808 | CCmdTarget_GetConnectionMap | function | Slot 15. |
| 0045180e | CCmdTarget_GetConnectionHook | function | `return NULL` (slot 21). |
| 00451813 | CCmdTarget_GetExtraConnectionPoints | function | `return FALSE` (slot 20). |
| 00451818 | CCmdTarget_GetCommandMap | function | Slot 13. |
| 00451844 | CCmdUI_Enable | function | EnableMenuItem or EnableWindow, moving focus away from a disabled control. |
| 0045193b | CCmdUI_SetRadio | function | SetCheck plus the menu dot bitmap (_AfxLoadDotBitmap / SetMenuItemBitmaps). |
| 0045198e | CCmdUI_SetText | function | ModifyMenu for a menu item, or AfxSetWindowText for a control. |
| 004519e5 | AfxNewHandler | function | `AfxThrowMemoryException(); return 0`. |
| 00451a4e | CWinApp_GetMessageMap | function | &CWinApp::messageMap. |
| 00451ac2 | CWinApp_ProcessWndProcException | function | CWinApp override (slot 29). |
| 00451cca | CWinThread_Delete | function | `if (m_bAutoDelete) delete this` (slot 32). |
| 00451cdc | CWinThread_InitInstance | function | `return FALSE` (slot 22). |
| 00451d5a | CWinThread_IsIdleMessage | function | Slot 27. |
| 00451dbe | CWinThread_ExitInstance | function | `return m_msgCur.wParam` (slot 28). |
| 00451f5e | CWinThread_PreTranslateMessage | function | Slot 24: tooltip relay, message filters, PreTranslate walk up to the main window. |
| 004522ce | CWinThread_GetMainWnd | function | m_pActiveWnd, then m_pMainWnd, then FromHandle(GetActiveWindow()) (slot 31). |
| 004522e9 | CWinThread_PumpMessage | function | GetMessage / PreTranslateMessage / Translate / Dispatch (slot 25). |
| 00452483 | CMenu_DrawItem | function | Empty virtual. |
| 00452486 | CMenu_MeasureItem | function | Empty virtual. |
| 00452489 | CObject_GetRuntimeClass | function | Returns classCObject (0x45bfc8). |
| 00452934 | CFile_Seek | function | SetFilePointer (slot 12). |
| 00452963 | CFile_GetPosition | function | Slot 5. |
| 0045298c | CFile_Flush | function | FlushFileBuffers (slot 20). |
| 004529f4 | CFile_Abort | function | CloseHandle without throwing; clears m_strFileName (slot 19). |
| 00452a17 | CFile_LockRange | function | LockFile (slot 17). |
| 00452a41 | CFile_UnlockRange | function | UnlockFile (slot 18). |
| 00452a6b | CFile_SetLength | function | Seek + SetEndOfFile (slot 13). |
| 00452ac5 | CFile_GetBufferPtr | function | `return 0` (slot 22). |
| 00453092 | CException_Delete | function | `if (m_bAutoDelete > 0) delete this`. |
| 004530a4 | CException_GetErrorMessage | function | Default: empty string, help context 0, `return FALSE`. |
| 00454461 | CDC_SetAttribDC | function | `m_hAttribDC = hDC`. |
| 0045446b | CDC_SetOutputDC | function | `m_hDC = hDC`. |
| 00454475 | CDC_ReleaseAttribDC | function | `m_hAttribDC = NULL`. |
| 0045447d | CDC_ReleaseOutputDC | function | `m_hDC = NULL`. |
| 00454485 | CDC_SaveDC | function | SaveDC on both DCs. |
| 004544c1 | CDC_RestoreDC | function | RestoreDC on both DCs. |
| 004548b1 | CDC_GetClipBox | function | `::GetClipBox(m_hDC, lpRect)`. |
| 00454d41 | AfxCriticalNewHandler | function | Shrinks or frees the thread's safety-pool buffer, else throws a memory exception. |
| 004550dc | CWinApp_SaveAllModified | function | m_pDocManager->SaveAllModified() (slot 36). |
| 004550f1 | CWinApp_AddToRecentFileList | function | AfxFullPath + m_pRecentFileList->Add (slot 34). |
| 0045512c | CWinApp_OpenDocumentFile | function | m_pDocManager->OpenDocumentFile (slot 33). |
| 0045513e | CWinApp_OnUpdateRecentFileMenu | function | Message-map UI handler. |
| 00455165 | CWinApp_OnDDECommand | function | m_pDocManager->OnDDECommand (slot 39). |
| 00455180 | CWinApp_OnOpenRecentFile | function | ID_FILE_MRU_FILE1 range handler. |
| 004553da | CPropertyPage_GetMessageMap | function | &CPropertyPage::messageMap. |
| 00455788 | CPropertyPage_OnApply | function | `OnOK(); return TRUE`. |
| 00455796 | CPropertyPage_OnReset | function | `OnCancel()`. |
| 0045579f | CPropertyPage_OnCancel | function | Empty virtual (slot 52). |
| 004557a0 | CPropertyPage_OnSetActive | function | First activation, else UpdateData(FALSE). |
| 004557bd | CPropertyPage_OnKillActive | function | `UpdateData(TRUE)`. |
| 004557cb | CPropertyPage_OnQueryCancel | function | `return TRUE`. |
| 004557d1 | CPropertyPage_OnWizardBack | function | `return 0`. |
| 004557d4 | CPropertyPage_OnWizardNext | function | `return 0`. |
| 004557d7 | CPropertyPage_OnWizardFinish | function | `return TRUE`. |
| 0045594e | CPropertyPage_OnNotify_switchTable | switch table | `pointer[10]`: PSN_* (code+0xd1) switch in CPropertyPage::OnNotify. |
| 00455976 | CPropertyPage_PreTranslateMessage | function | `CWnd::PreTranslateMessage(pMsg); return FALSE`. |
| 004559ff | CPropertySheet_GetMessageMap | function | &CPropertySheet::messageMap. |
| 00455c49 | CPropertySheet_PreTranslateMessage | function | Slot 38. |
| 00455f05 | CPropertySheet_BuildPropPageArray | function | Slot 49. |
| 00455f88 | CPropertySheet_GetActiveIndex | function | m_psh.nStartPage, or TabCtrl_GetCurSel(GetTabControl()). |
| 004564cf | CComboBox_DrawItem | function | Empty virtual. |
| 004564d2 | CComboBox_MeasureItem | function | Empty virtual. |
| 004564d5 | CComboBox_CompareItem | function | `return 0`. |
| 004564da | CComboBox_DeleteItem | function | Empty virtual. |
| 004565bc | Mfc_GdiBrushTermObj_init | function | `$E` stub: Mfc_GdiBrushTermObj_init_ctor + _init_atexit. |
| 004565d5 | AfxWingdixTerm | function | atexit target: AfxDeleteObject(&_afxHalftoneBrush) (MFC wingdix.cpp). |
| 004565e0 | CDialog_GetRuntimeClass | function | Returns classCDialog (0x45aae0). |
| 00456643 | CWnd_GetRuntimeClass | function | Returns classCWnd (0x45af08). |
| 00456729 | CCmdTarget_GetRuntimeClass | function | "CCmdTarget". |
| 0045672f | CPropertyPage_GetRuntimeClass | function | CPropertyPage vtable slot 0. |
| 00456735 | CPropertySheet_GetRuntimeClass | function | CPropertySheet vtable slot 0. |
| 0045673b | CSliderCtrl_GetRuntimeClass | function | "CSliderCtrl". |
| 0045679e | CImageList_GetRuntimeClass | function | "CImageList". |
| 004567a4 | CStatic_GetRuntimeClass | function | "CStatic". |
| 004567aa | CButton_GetRuntimeClass | function | "CButton". |
| 004567b0 | CComboBox_GetRuntimeClass | function | "CComboBox". |
| 004567b6 | CEdit_GetRuntimeClass | function | "CEdit". |
| 00456954 | CWinApp_InitApplication | function | Takes over CDocManager::pStaticDocManager (slot 35). |
| 0045699d | CWinApp_InitInstance | function | `return TRUE` (slot 22). |
| 00456ed6 | CWinApp_GetRuntimeClass | function | "CWinApp". |
| 00456edc | _afxWinState_init | function | `$E` stub: _init_ctor + _init_atexit. |
| 00456ef5 | _afxWinState_atexitDtor | function | atexit target. It sets up an EH frame and calls the Unwind0 funclet that destroys _afxWinState. |
| 00456fd6 | CWinThread_GetRuntimeClass | function | "CWinThread". |
| 0045704f | CMenu_GetRuntimeClass | function | "CMenu". |
| 0045717a | _afxThreadState_init | function | `$E` stub. |
| 00457193 | _afxThreadState_atexitDtor | function | atexit destructor stub. |
| 0045767e | _afxBaseModuleState_init | function | `$E` stub. |
| 00457697 | _afxBaseModuleState_atexitDtor | function | atexit destructor stub. |
| 004579e2 | CFile_GetRuntimeClass | function | "CFile". |
| 004579e8 | CResourceException_GetRuntimeClass | function | "CResourceException". |
| 004579ee | _simpleResourceException_init | function | `$E` stub. |
| 00457a86 | _simpleResourceException_atexitDtor | function | atexit destructor stub (resets the vptr, destroys the object). |
| 00457ac7 | CUserException_GetRuntimeClass | function | "CUserException". |
| 00457acd | _simpleUserException_init | function | `$E` stub. |
| 00457b65 | _simpleUserException_atexitDtor | function | atexit destructor stub. |
| 00457bfd | CDC_CreateObject_Unwind0_tail | data | 6 bytes (`call CObject_operator_delete; ret`). They belong to CDC_CreateObject_Unwind0, but Ghidra doesn't disassemble them, so they are typed `undefined1[6]`. |
| 00457c03 | CDC_GetRuntimeClass | function | "CDC". |
| 00457c09 | CPaintDC_GetRuntimeClass | function | "CPaintDC". |
| 00457c7c | CGdiObject_CreateObject_Unwind0_tail | label | Same 6-byte funclet tail for CGdiObject_CreateObject_Unwind0. Undefined bytes with a label. |
| 00457c82 | CGdiObject_GetRuntimeClass | function | "CGdiObject". |
| 00457c88 | CPen_GetRuntimeClass | function | "CPen". |
| 00457c8e | CException_GetRuntimeClass | function | "CException". |
| 00457c94 | CMemoryException_GetRuntimeClass | function | "CMemoryException". |
| 00457c9a | _simpleMemoryException_init | function | `$E` stub. |
| 00457d32 | _simpleMemoryException_atexitDtor | function | atexit destructor stub. |
| 00457d73 | CNotSupportedException_GetRuntimeClass | function | "CNotSupportedException". |
| 00457d79 | _simpleNotSupportedException_init | function | `$E` stub. |
| 00457e11 | _simpleNotSupportedException_atexitDtor | function | atexit destructor stub. |
| 0045846a | CMapPtrToPtr_GetRuntimeClass | function | "CMapPtrToPtr". |
| 0045852c | _afxCtl3dThread_init | function | `$E` stub. |
| 00458545 | _afxCtl3dThread_atexitDtor | function | atexit destructor stub. |
| 0045857c | _afxCtl3dState_init | function | `$E` stub. |
| 00458595 | _afxCtl3dState_atexitDtor | function | atexit destructor stub. |
| 004585cc | afxData_init | function | `$E` stub: afxData_init_ctor + afxData_init_atexit. |
| 004585ee | afxData_atexitDtor | function | `mov ecx,&afxData; jmp AUX_DATA_dtor`. |
| 004587bb | _afxPropPageFontInfo_init | function | `$E` stub. |
| 004587d4 | _afxPropPageFontInfo_atexitDtor | function | atexit destructor stub. |
| 00458926 | CPtrArray_GetRuntimeClass | function | "CPtrArray". |
| 0045892c | CPtrList_GetRuntimeClass | function | "CPtrList". |
| 00458932 | CFileException_GetRuntimeClass | function | "CFileException". |
| 00458bf6 | AUX_DATA_dtor | function | `AUX_DATA::~AUX_DATA`: AfxDeleteObject(&hbmMenuDot). Reached only as a jump target. |
| 0044c5c0 | Ctl3d_EditWndProc3d | function | CTL3D Edit subclass proc (class table 0x45d3dc+0x40). Calls Ctl3d_EditComboWndProcCommon(…,ctEdit=2). |
| 0044cb20 | Ctl3d_StaticWndProc3d | function | CTL3D Static subclass proc (class table entry "Static"). |

</details>

<details><summary>Globals 0x45AC78–0x46948C (164)</summary>

| Address | Name | Type | What it is |
|---|---|---|---|
| 0x0045ac78 | CWnd_messageMap | AFX_MSGMAP (pBaseMap,lpEntries) | CWnd::messageMap (base = CCmdTarget::messageMap 0x45b048, entries 0x45ac80 start with WM_CTLCOLORSTATIC->OnNTCtlColor); returned by CWnd::GetMessageMap 0x44f7fd |
| 0x0045af08 | classCWnd | CRuntimeClass | CWnd::classCWnd ("CWnd", size 0x3C, schema 0xFFFF, CreateObject 0x4565e6, base classCCmdTarget 0x45b128); used by afxMapHWND |
| 0x0045b120 | rgbDot | BYTE[5] | rgbDot {06 0F 0F 0F 06} radio-dot bitmap rows used by _AfxLoadDotBitmap |
| 0x0045b121 | rgbDot_1 | byte | rgbDot[1] (loop-pointer artifact inside rgbDot) |
| 0x0045b1cc | _pfnUninitialized | _PNH (-1) | MFC afxmem.cpp _pfnUninitialized sentinel (0xFFFFFFFF) compared in operator new |
| 0x0045b308 | _afxPropSheetIDs | int[4] | {ID_WIZNEXT,ID_WIZFINISH,ID_WIZBACK,IDCANCEL} scanned by CPropertySheet::OnSetDefID |
| 0x0045b30c | _afxPropSheetIDs_1 | int | _afxPropSheetIDs[1] (ID_WIZFINISH) interior artifact |
| 0x0045bd50 | CWinApp_messageMap | AFX_MSGMAP | CWinApp::messageMap (base CCmdTarget, entries 0x45bd58: WM_COMMAND ID_APP_EXIT...); returned by CWinApp::GetMessageMap 0x451a4e |
| 0x0045c1d0 | classCDC | CRuntimeClass | CDC::classCDC ("CDC", size 0x10, base CObject); used by afxMapHDC |
| 0x0045c248 | classCPen | CRuntimeClass | CPen::classCPen ("CPen", size 8, base classCGdiObject 0x45c230); returned by CPen::GetRuntimeClass 0x457c88 |
| 0x0045c6a0 | ExceptionTemplate | EHExceptionRecord | throw.cpp ExceptionTemplate {0xE06D7363 'msc', EXCEPTION_NONCONTINUABLE,...} copied by _CxxThrowException |
| 0x0045c6a4 | ExceptionTemplate_ExceptionFlags | dword | ExceptionTemplate.ExceptionFlags (=1) interior |
| 0x0045c82c | g_CrtWStr_Null | wchar_t[7] | L"(null)" - target of __wnullstring (0x4677d4) in _output |
| 0x0045c83c | g_CrtStr_Null | char[7] | "(null)" - target of __nullstring (0x4677d0) in _output |
| 0x0045c8f0 | g_CrtStr_ExpTemplate_e000 | char[6] | "e+000" exponent template copied (dword+word) by _cftoe2 |
| 0x0045c8f4 | g_CrtStr_ExpTemplate_e000_tail | word | last 2 bytes ("0\0") of "e+000" (inlined strcpy artifact) |
| 0x0045cbe4 | g_CrtStr_NewlineNewline | char[3] | "\n\n" used by _NMSG_WRITE when building the runtime-error message box text |
| 0x0045ccd4 | g_CrtStr_Dot | char[2] | "." default decimal point strcpy'd by __init_numeric for the C locale |
| 0x0045d2c4 | g_CrtStr_1QNAN | char[7] | "1#QNAN" ($I10_OUTPUT special-value string) |
| 0x0045d2c8 | g_CrtStr_1QNAN_4 | word | "1#QNAN"+4 (inlined strcpy artifact) |
| 0x0045d2ca | g_CrtStr_1QNAN_6 | byte | "1#QNAN"+6 terminator (inlined strcpy artifact) |
| 0x0045d2cc | g_CrtStr_1INF | char[6] | "1#INF" ($I10_OUTPUT) |
| 0x0045d2d0 | g_CrtStr_1INF_4 | word | "1#INF"+4 (inlined strcpy artifact) |
| 0x0045d2d4 | g_CrtStr_1IND | char[6] | "1#IND" ($I10_OUTPUT) |
| 0x0045d2d8 | g_CrtStr_1IND_4 | word | "1#IND"+4 (inlined strcpy artifact) |
| 0x0045d2dc | g_CrtStr_1SNAN | char[7] | "1#SNAN" ($I10_OUTPUT) |
| 0x0045d2e0 | g_CrtStr_1SNAN_4 | word | "1#SNAN"+4 (inlined strcpy artifact) |
| 0x0045d2e2 | g_CrtStr_1SNAN_6 | byte | "1#SNAN"+6 terminator |
| 0x0045d3e4 | g_Ctl3d_mpicdef_msk | word | UNCERTAIN (table/field names are CTL3D-style guesses): CTL3D (static ctl3d32s, via Enable3dControlsStatic) control-class table: 0x20-byte entries {char sz[20] "Button"/"ListBox"/"Edit"/"ComboBox"/"Static"/"ComboLBox"; pfn; pfnCanSubclass; WORD msk}, table base 0x45d3c8; this is entry0.msk (CTL3D_BUTTONS=1) - FUN_0044b3a0 iterates via this field |
| 0x0045d404 | g_Ctl3d_mpicdef_1_msk | word | UNCERTAIN (table/field names are CTL3D-style guesses): CTL3D class table entry1 ("ListBox").msk = 2 |
| 0x0045d488 | g_Ctl3d_mpicvSysColors | WORD[8] | CTL3D system-colour index table {BTNHIGHLIGHT,BTNFACE,BTNSHADOW,BTNTEXT,WINDOW,WINDOWTEXT,GRAYTEXT,WINDOWFRAME} read by FUN_0044aac0 (Ctl3d colour refresh) |
| 0x0045d48a | g_Ctl3d_mpicvSysColors_1 | word | mpicvSysColors[1] (COLOR_BTNFACE) interior |
| 0x00462504 | g_AmbientDefs_m1_nRight | dword | UNCERTAIN (phantom xref; real bytes are padding before g_AmbientDefs): GAME data, not CRT: phantom xref target = g_AmbientDefs[-1].nRight ([EDI+0x462518] with index -1 in Sound_StartAmbient/0x40df7a); real bytes are padding before g_AmbientDefs (0x462508) |
| 0x00464598 | g_ControlSchemeMasks_f10 | dword | UNCERTAIN (field meaning of g_ControlSchemeMasks+0x10 unknown): GAME data: g_ControlSchemeMasks (0x464588)+0x10, read via [EDX+0x10] in Player_Update (EDX points to the scheme record) |
| 0x0046459c | g_ControlSchemeMasks_f14 | dword | UNCERTAIN (field meaning of g_ControlSchemeMasks+0x14 unknown): GAME data: g_ControlSchemeMasks+0x14, read via [EDX+0x14] in Player_Update |
| 0x00464a00 | g_CreditsPage20_Lines | char*[2] | GAME data: credits line list {"GAME", NULL}; pointed to by credits page table PTR_PTR_00464ad0[20] (Credits_Update/TextBlock_Spawn) |
| 0x00464b2a | g_PlaneDefaultDepths_1 | short | UNCERTAIN (table name g_PlaneDefaultDepths is descriptive guess): GAME data: element [1] of unnamed short table at 0x464b28 {0x90,0x40,0x20,0x10,...} of default plane depths used by Planes_UpdateDepth |
| 0x00466438 | g_LevelDefs_m1_dwPlaneMask | dword | GAME data: phantom xref = g_LevelDefs[-1].dwPlaneMask (g_LevelDefs at 0x466450, stride 0x18; Ghidra assumed index -1); bytes belong to the preceding game word-table |
| 0x0046643c | g_LevelDefs_m1_dwActivePlaneMask | dword | GAME data: phantom g_LevelDefs[-1].dwActivePlaneMask |
| 0x00466440 | g_LevelDefs_m1_bJingleFlags | byte | GAME data: phantom g_LevelDefs[-1].bJingleFlags |
| 0x00466441 | g_LevelDefs_m1_cAmbientIndex | byte | GAME data: phantom g_LevelDefs[-1].cAmbientIndex |
| 0x00466442 | g_LevelDefs_m1_bWldIndex | byte | GAME data: phantom g_LevelDefs[-1].bWldIndex (14 reads, all g_LevelDefs[x].bWldIndex) |
| 0x00466443 | g_LevelDefs_m1_bRotatingPlanes | byte | GAME data: phantom g_LevelDefs[-1].bRotatingPlanes |
| 0x00466c98 | _afxPropSheetButtons | int[4] | {IDOK, IDCANCEL, ID_APPLY_NOW, IDHELP} iterated in CPropertySheet::OnInitDialog |
| 0x00466c9c | _afxPropSheetButtons_1 | int | _afxPropSheetButtons[1] interior |
| 0x00466da8 | CDocManager_pStaticDocManager | CDocManager* | CDocManager::pStaticDocManager (deleted in ~CWinApp, handed to m_pDocManager in CWinApp::InitApplication) |
| 0x00466dac | CDocManager_pStaticList | CPtrList* | CDocManager::pStaticList (deleted in ~CWinApp) |
| 0x00467080 | _afxDataNil | CStringData* | strcore.cpp _afxDataNil -> _afxInitData {-1,0,0,0} at 0x467070; compared in CString::Release/~CString |
| 0x00467084 | _afxPchNil | LPCTSTR | strcore.cpp _afxPchNil (=_afxInitData+12); AfxGetEmptyString returns its address (afxEmptyString) |
| 0x004671d4 | __NLG_Destination_uoffDestination | dword | __NLG_Destination (0x4671d0, sig 0x19930520).uoffDestination written by _NLG_Notify |
| 0x004671d8 | __NLG_Destination_dwCode | dword | __NLG_Destination.dwCode |
| 0x004671dc | __NLG_Destination_uoffFramePointer | dword | __NLG_Destination.uoffFramePointer |
| 0x004671e8 | __fastflag | int | x87 transcendental dispatcher fast flag; when nonzero skips _87except error handling |
| 0x00467200 | _aenvptr | char* | environment block from __crtGetEnvironmentStringsA (set in WinMainCRTStartup, freed by _setenvp) |
| 0x0046720c | __error_mode | int | _set_error_mode value (_OUT_TO_DEFAULT/STDERR/MSGBOX) used by _NMSG_WRITE/_FF_MSGBANNER/_amsg_exit |
| 0x00467210 | __app_type | int | _CONSOLE_APP/_GUI_APP; used by _NMSG_WRITE and _set_osfhnd/_free_osfhnd (SetStdHandle) |
| 0x00467240 | _umaskval | int | _umask value applied in _sopen |
| 0x00467244 | _osver | unsigned | GetVersion()>>16 (entry also fills _winver 0x467248, _winmajor 0x46724c, _winminor 0x467250) |
| 0x00467254 | __argc | int | argument count (_setargv, CWinApp::ParseCommandLine) |
| 0x00467258 | __argv | char** | argument vector |
| 0x00467260 | _environ | char** | narrow environment table |
| 0x00467264 | __initenv | char** | initial environment pointer compared in __crtsetenv |
| 0x00467268 | _wenviron | wchar_t** | wide environment table (__crtsetenv, __wtomb_environ, getenv) |
| 0x00467280 | _C_Exit_Done | int | doexit: TerminateProcess if already 1, set to 1 before ExitProcess (_C_Termination_Done 0x46727c, _exitflag 0x467278 next to it) |
| 0x00467290 | _mbctype | unsigned char[257] | multibyte ctype table (memset/filled by _setmbcp/setSBCS; lookups use _mbctype+1) |
| 0x00467294 | _mbctype_4 | byte | _mbctype+4 (rep-stos artifact of the memset in _setmbcp/setSBCS) |
| 0x00467394 | __mbcodepage | int | current multibyte code page (0 = SBCS; tested by _mbschr/_mbscmp/_mbsrchr...) |
| 0x00467398 | __mblcid | int | LCID for current MB code page (used by _mbsnbicoll) |
| 0x004673a0 | __mbulinfo | unsigned short[6] | MB upper/lower range info, cleared/filled by _setmbcp/setSBCS |
| 0x004673a4 | __mbulinfo_2 | word pair | __mbulinfo[2..3] |
| 0x004673a8 | __mbulinfo_4 | word pair | __mbulinfo[4..5] |
| 0x004673ac | fSystemSet | int | mbctype.c static: set by getSystemCP when _MB_CP_ANSI/OEM/LOCALE requested |
| 0x004673b0 | __rgctypeflag | unsigned char[4] | {_M1? 1,2,4,8} ctype flag per range class used by _setmbcp |
| 0x004673b1 | __rgctypeflag_1 | byte | __rgctypeflag[1] |
| 0x004673b8 | __rgcode_page_info | code_page_info[5] | 0x30-byte entries {code_page 932/936/949/950/1361, mbulinfo[6], rgrange[4][8]} |
| 0x004673e8 | __rgcode_page_info_1 | code_page_info | entry 1 (CP 936) - loop artifact |
| 0x004674a8 | errtable | errentry[45] | dosmap.c {oscode, errno} table ({1,EINVAL},{2,ENOENT},...), end 0x467610; scanned by _dosmaperr (sits right after __rgcode_page_info) |
| 0x004674b0 | errtable_1 | errentry | errtable[1] |
| 0x004674b4 | errtable_1_errnocode | int | errtable[1].errnocode ([ESI*8+0x4674ac] artifact) |
| 0x00467638 | __lc_time_c | struct __lc_time_data | C-locale time-name data; assigned to __lc_time_curr (0x4676e4) by __init_time |
| 0x00467700 | __tlsindex | DWORD | TLS slot for _tiddata (init 0xFFFFFFFF; _mtinit/_getptd) |
| 0x0046770c | _locktable_SIGNAL_LOCK | CRITICAL_SECTION* | _locktable[1] (_locktable base 0x467708), preallocated, initialised by _mtinitlocks |
| 0x0046772c | _locktable_HEAP_LOCK | CRITICAL_SECTION* | _locktable[9] |
| 0x0046773c | _locktable_EXIT_LOCK1 | CRITICAL_SECTION* | _locktable[13] |
| 0x0046774c | _locktable_LOCKTAB_LOCK | CRITICAL_SECTION* | _locktable[17] |
| 0x004677c8 | _stdbuf | char*[2] | temporary stdout/stderr buffers used by _stbuf |
| 0x004677d0 | __nullstring | char* | -> "(null)" for printf %s NULL |
| 0x004677d4 | __wnullstring | wchar_t* | -> L"(null)" |
| 0x0046787c | _newmode | int | malloc new-handler mode (_set_new_mode) read by malloc/calloc/realloc |
| 0x00467890 | __small_block_heap_p_starting_alloc_map | dword | __small_block_heap (static first SBH region at 0x467888, size 0x814).p_starting_alloc_map |
| 0x00467894 | __small_block_heap_p_first_uncommitted | dword | __small_block_heap.p_first_uncommitted |
| 0x00467898 | __small_block_heap_alloc_map | byte[0x400] | __small_block_heap.alloc_map[] (free paras per page; init 0xF0/0xFF) |
| 0x00467899 | __small_block_heap_alloc_map_1 | byte | alloc_map[1] |
| 0x0046789a | __small_block_heap_alloc_map_2 | byte | alloc_map[2] |
| 0x00467c96 | __small_block_heap_alloc_map_3FE | byte | alloc_map[0x3FE] |
| 0x00467c97 | __small_block_heap_alloc_map_3FF | byte | alloc_map[0x3FF] (last) |
| 0x00467c98 | __small_block_heap_last_failed_alloc | byte[0x400] | __small_block_heap.last_failed_alloc[] (init 0xF1) |
| 0x00467c99 | __small_block_heap_last_failed_alloc_1 | byte | last_failed_alloc[1] |
| 0x00468098 | __small_block_heap_p_pages_begin | void* | __small_block_heap.p_pages_begin (0 = static region unused; checked in __sbh_new_region/__sbh_release_region) |
| 0x004680a0 | __sbh_decommitable_pages | int | count of free pages pending decommit |
| 0x004680a4 | __sbh_threshold | size_t | SBH size threshold (0x1E0) checked by _heap_alloc/_calloc/_realloc/_expand (preceded by __sbh_p_starting_region 0x46809c) |
| 0x004681a4 | __badioinfo_osfile | char | __badioinfo (0x4681a0 {-1,0,'\n'}).osfile, used by _flsbuf/_filbuf for fh==-1 |
| 0x00468240 | _First_FPE_Indx | int | =3, first FP exception index in _XcptActTab |
| 0x00468244 | _Num_FPE | int | =7 FP exception entries |
| 0x0046824c | _XcptActTabCount | int | =10 entries in _XcptActTab (after _XcptActTabSize 0x468248) |
| 0x00468254 | f_use_crtGetEnvironmentStrings | int | static f_use in __crtGetEnvironmentStringsA (USE_W/USE_A probe) |
| 0x00468258 | _rterrs | rterrmsgs[18] | {errno, text*} runtime-error message table used by _NMSG_WRITE |
| 0x00468260 | _rterrs_1 | rterrmsgs | _rterrs[1] (R6008) |
| 0x00468268 | _rterrs_2 | rterrmsgs | _rterrs[2] |
| 0x004682e8 | _adbgmsg | void(*)(void) | debug-message hook called by _FF_MSGBANNER; also end bound of _rterrs |
| 0x004682f0 | _pctype | unsigned short* | = _ctype+1 (0x4682fa); replaced by __init_ctype |
| 0x004682f4 | _pwctype | wctype_t* | = _ctype+1 |
| 0x004684fc | __mb_cur_max | int | max bytes per MB char (1), set by __init_ctype |
| 0x00468500 | __decimal_point | char | '.' current decimal point (set by __init_numeric; used by _cftoe/_cftof/strtold) (followed by __decimal_point_length 0x468504) |
| 0x00468518 | __lc_handle_LC_CTYPE | LCID | __lc_handle[LC_CTYPE] (array base 0x468510) |
| 0x0046851c | __lc_handle_LC_MONETARY | LCID | __lc_handle[LC_MONETARY] (set by __init_monetary) |
| 0x00468520 | __lc_handle_LC_NUMERIC | LCID | __lc_handle[LC_NUMERIC] tested by __init_numeric |
| 0x00468524 | __lc_handle_LC_TIME | LCID | __lc_handle[LC_TIME] (__lc_handle base 0x468510) tested by __init_time |
| 0x00468528 | __lc_codepage | UINT | locale ANSI code page used by __crtCompareStringA/__crtGetLocaleInfoA/__crtGetStringTypeA... |
| 0x00468530 | _timezone | long | 28800 (PST) seconds west of UTC |
| 0x00468534 | _daylight | int | 1 = DST observed |
| 0x00468538 | _dstbias | long | -3600 |
| 0x00468543 | tzstd_3 | char | tzstd[3] (tzstd = char[64] "PST" at 0x468540, pointed to by _tzname[0]); strncpy/terminator artifact |
| 0x0046857f | tzstd_63 | char | tzstd[63] (forced terminator in _tzset) |
| 0x00468580 | tzdst | char[64] | "PDT" DST zone-name buffer (_tzname[1]) |
| 0x00468583 | tzdst_3 | char | tzdst[3] |
| 0x004685bf | tzdst_63 | char | tzdst[63] |
| 0x004685c0 | _tzname | char*[2] | _tzname[0] -> tzstd |
| 0x004685c4 | _tzname_1 | char* | _tzname[1] -> tzdst |
| 0x004685c8 | lastTZ | char* | tzset.c static copy of last TZ env string |
| 0x004685d0 | dststart_yr | int | tzset.c static transitiondate dststart.yr (-1 = not computed) |
| 0x004685d4 | dststart_yd | int | dststart.yd |
| 0x004685d8 | dststart_ms | long | dststart.ms |
| 0x004685e0 | dstend_yr | int | dstend.yr |
| 0x004685e4 | dstend_yd | int | dstend.yd |
| 0x004685e8 | dstend_ms | long | dstend.ms |
| 0x004685ec | first_time_tzset | int | __tzset static first_time (double-checked under _TIME_LOCK); also = _lpdays-4 base used by cvtdate |
| 0x004685f4 | _lpdays_1 | int | _lpdays[1] (_lpdays at 0x4685f0 = {-1,30,59,...,365}, leap-year cumulative days) |
| 0x004685f8 | _lpdays_2 | int | _lpdays[2] |
| 0x00468628 | _days | int[13] | {-1,30,58,89,...,364} non-leap cumulative days (_make_time_t, _gmtime, cvtdate) |
| 0x0046862c | _days_1 | int | _days[1] |
| 0x00468630 | _days_2 | int | _days[2] |
| 0x00468634 | _days_3 | int | _days[3] |
| 0x004687d0 | _iob | FILE[20] | stdio stream table (stdin uses static _bufin 0x5a5590); range check in _lock_file/_unlock_file |
| 0x00468a60 | _commode | int | default commit mode OR'ed into stream flags by _openfile |
| 0x00468c30 | pfnMessageBox | FARPROC | __crtMessageBoxA static, lazily GetProcAddress'd from user32 |
| 0x00468c34 | pfnGetActiveWindow | FARPROC | __crtMessageBoxA static |
| 0x00468c38 | pfnGetLastActivePopup | FARPROC | __crtMessageBoxA static |
| 0x00468c44 | f_use_crtLCMapStringA | int | static f_use (W/A API probe) in __crtLCMapStringA |
| 0x00468c48 | f_use_crtGetStringTypeW | int | static f_use in __crtGetStringTypeW |
| 0x00468c4c | f_use_crtGetStringTypeA | int | static f_use in __crtGetStringTypeA |
| 0x00468c50 | __lc_time_intl | __lc_time_data* | __init_time static: currently allocated non-C time locale data |
| 0x00468c54 | dec_pnt | char* | __init_numeric static (LOCALE_SDECIMAL) |
| 0x00468c58 | thousands_sep | char* | __init_numeric static (LOCALE_STHOUSAND) |
| 0x00468c5c | grouping | char* | __init_numeric static (LOCALE_SGROUPING, passed to fix_grouping) |
| 0x00468c60 | __lconv_intl | struct lconv* | UNCERTAIN (exact CRT source name of the __init_monetary static not verified): the non-C lconv allocated by __init_monetary (freed and replaced on each call) |
| 0x00468c64 | ctype1 | unsigned short* | __init_ctype static: allocated char ctype table (_pctype = ctype1+1) |
| 0x00468c68 | wctype1 | unsigned short* | __init_ctype static: allocated wide ctype table (_pwctype = wctype1+1) |
| 0x00468c7c | __lc_id_LC_CTYPE_wLanguage | WORD | __lc_id[LC_CTYPE].wLanguage (__lc_id = LC_ID[6] at 0x468c70) |
| 0x00468c84 | __lc_id_LC_MONETARY_wCountry | WORD | __lc_id[LC_MONETARY].wCountry |
| 0x00468c8a | __lc_id_LC_NUMERIC_wCountry | WORD | __lc_id[LC_NUMERIC].wCountry |
| 0x00468c8e | __lc_id_LC_TIME_wLanguage | WORD | __lc_id[LC_TIME].wLanguage |
| 0x00468c90 | __lc_id_LC_TIME_wCountry | WORD | __lc_id[LC_TIME].wCountry |
| 0x0046948c | _fmode | int | default file translation mode; _sopen compares with _O_BINARY (0x8000) |

</details>

<details><summary>Globals 0x469490–0x5A71E5 (164)</summary>

| Address | Name | Type | What it is |
|---|---|---|---|
| 0x00469490 | ctrlc_action | undefined4 | CRT signal.c: SIGINT handler (raise case 2). C |
| 0x00469494 | ctrlbreak_action | undefined4 | CRT signal.c: SIGBREAK (21) handler. C |
| 0x00469498 | abort_action | undefined4 | CRT signal.c: SIGABRT (22) handler. C |
| 0x0046949c | term_action | undefined4 | CRT signal.c: SIGTERM (15) handler. C |
| 0x00469580 | _matherr_flag | undefined4 | CRT: value 9876 (0x2694) means "default _matherr, don't call it"; tested in _87except and _cintrindisp1/2. C |
| 0x004695a8 | _d_inf | undefined4 | CRT util.c `_d_inf` low dword (double +INF 0x7FF0000000000000), used by _handle_exc. C |
| 0x004695ac | _d_inf_hi | undefined4 | high dword of `_d_inf`. C |
| 0x004695b8 | _d_max | undefined4 | CRT `_d_max` low dword (DBL_MAX 0x7FEFFFFFFFFFFFFF). C |
| 0x004695bc | _d_max_hi | undefined4 | high dword of `_d_max`. C |
| 0x004695d8 | __lconv_c | pointer | CRT `__lconv_c` (static C-locale struct lconv), field decimal_point (-> "." at 0x4695d0); `__lconv` (0x469608) points here. C |
| 0x004695dc | __lconv_c_thousands_sep | pointer | `__lconv_c.thousands_sep` (-> "" at 0x4695d4). C |
| 0x004695e0 | __lconv_c_grouping | undefined * | `__lconv_c.grouping`. C |
| 0x0046960c | f_use_GetLocaleInfoW | undefined4 | static `f_use` in __crtGetLocaleInfoW (1 = use W API, 2 = use A API). C |
| 0x00469610 | f_use_GetLocaleInfoA | undefined4 | static `f_use` in __crtGetLocaleInfoA. C |
| 0x00469618 | f_use_CompareStringA | undefined4 | static `f_use` in __crtCompareStringA. C |
| 0x00469ee4 | g_D3DTexFmtAlphaOk_IdxM1 | undefined4 | GAME, phantom: element [-1] of the D3D texture-format "alpha usable" array at 0x469ee8 (D3D_InitDevice, index g_D3DTexFormatIndex = -1). U |
| 0x0046a1b4 | g_D3DTexFmtHasAlpha_IdxM1 | undefined4 | GAME, phantom: element [-1] of the texture-format "has 0x8000 alpha" array at 0x46a1b8 (D3D_InitDevice). U |
| 0x00471b34 | g_PaletteDIB0_biPlanes | undefined2 | GAME: first palette DIB (BITMAPINFO at 0x471b28, stride 0x528) biPlanes, World_LoadWLD. C |
| 0x00471b36 | g_PaletteDIB0_biBitCount | undefined2 | same, biBitCount (4 or 8). C |
| 0x00471b38 | g_PaletteDIB0_biCompression | undefined4 | same, biCompression. C |
| 0x00471b3c | g_PaletteDIB0_biSizeImage | undefined4 | same, biSizeImage. C |
| 0x00471b40 | g_PaletteDIB0_biXPelsPerMeter | undefined4 | same, biXPelsPerMeter. C |
| 0x00471b44 | g_PaletteDIB0_biYPelsPerMeter | undefined4 | same, biYPelsPerMeter. C |
| 0x00471b48 | g_PaletteDIB0_biClrUsed | undefined4 | same, biClrUsed. C |
| 0x00471b4c | g_PaletteDIB0_biClrImportant | undefined4 | same, biClrImportant. C |
| 0x00471b51 | g_PaletteDIB0_Color0_rgbGreen | undefined1 | phantom: g_PaletteDIBs_bmiColors[0].rgbGreen (palette-conversion loop). C |
| 0x00471b52 | g_PaletteDIB0_Color0_rgbRed | undefined1 | phantom: bmiColors[0].rgbRed. C |
| 0x00471b53 | g_PaletteDIB0_Color0_rgbReserved | undefined1 | phantom: bmiColors[0].rgbReserved. C |
| 0x00471b54 | g_PaletteDIB0_Color1_rgbBlue | undefined1 | phantom: bmiColors[1].rgbBlue. C |
| 0x00471b55 | g_PaletteDIB0_Color1_rgbGreen | undefined1 | phantom: bmiColors[1].rgbGreen. C |
| 0x00471b56 | g_PaletteDIB0_Color1_rgbRed | undefined1 | phantom: bmiColors[1].rgbRed. C |
| 0x00477220 | g_GameTextIndex_Lang1_Ofs | undefined2 | GAME, phantom: GAME.TXT index table (0x4770b8, u16 ofs/u16 len) entry 90 = first string of language block 1, offset. C |
| 0x00477222 | g_GameTextIndex_Lang1_Len | undefined2 | same entry, length. C |
| 0x00477388 | g_GameTextIndex_Lang2_Ofs | undefined2 | entry 180 (language block 2), offset. C |
| 0x0047738a | g_GameTextIndex_Lang2_Len | undefined2 | entry 180, length. C |
| 0x004777dc | g_SpriteQueueBucket0 | undefined4 | GAME: sprite priority bucket 0 list head (g_pSpriteQueue = 0x4777d8, buckets at +4), Game_Frame. C |
| 0x004777e0 | g_SpriteQueueBucket1 | undefined4 | sprite priority bucket 1 list head. C |
| 0x00498f7c | g_ObjTypes_1 | undefined4 | GAME, phantom: g_ObjTypes[1] (type table 0x498f78). C |
| 0x00499378 | g_ObjTypes_Main0 | undefined4 | phantom: g_ObjTypes[0x100] (first MAIN.WLD type). C |
| 0x0049937c | g_ObjTypes_Main1 | undefined4 | phantom: g_ObjTypes[0x101]. C |
| 0x00499880 | g_ActorLinkPool_1_pNext | undefined4 | GAME, phantom: actor link pool (0x499778, 32 x 0x108) element 1, +0 next (Actor_AllocLink). C |
| 0x00499884 | g_ActorLinkPool_1_nIndex | undefined4 | same element, +4 slot index. C |
| 0x0049c4e0 | g_Planes20_nMaxScrollX | undefined2 | GAME, phantom: g_Planes[20] (one past Plane[20]) .nMaxScrollX, Level_InitPlanes loop. C |
| 0x0049c4e2 | g_Planes20_nMaxScrollY | undefined2 | g_Planes[20].nMaxScrollY. C |
| 0x0049c4e6 | g_Planes20_nAngle | undefined2 | g_Planes[20].nAngle. C |
| 0x0049c4ec | g_Planes20_nRotating | undefined4 | g_Planes[20].nRotating. C |
| 0x0049c4f0 | g_Planes20_nOrbitPos | undefined2 | g_Planes[20].nOrbitPos. C |
| 0x004ac4c8 | g_FileBufferStorage | undefined4 | GAME: static file-read buffer; g_pFileBuffer (0x4645d4) is initialised to point here. C |
| 0x004ac4cc | g_FileBufferStorage_4 | undefined4 | phantom: dword +4 of the file buffer (File_CompactBuffer / demo copy loops). C |
| 0x0052c4d4 | g_ParallaxDrawList_1 | undefined4 | GAME, phantom: g_ParallaxDrawList[1] (0x52c4d0), Actors_DrawAndCull. C |
| 0x0052c709 | g_LevelHeap_1 | undefined1 | GAME, phantom: g_LevelHeap+1 (0x52c708), text/4bpp unpack loops. C |
| 0x0052c70a | g_LevelHeap_2 | undefined1 | phantom: g_LevelHeap+2. C |
| 0x0059c75c | g_DemoInputBuffer_1 | undefined4 | GAME, phantom: g_DemoInputBuffer (0x59c758) dword [1], Game_SetupNextLevel copy loop. C |
| 0x0059f414 | g_MixerSamples_1 | undefined4 | GAME sound lib: mixer sample table (0x59f400, 128 x 20 B) element 1 .pData. C |
| 0x0059f418 | g_MixerSampleAltData_1 | undefined4 | element 1 .pAltData (mode-4 second channel). C |
| 0x0059f41c | g_MixerSampleSize_1 | undefined4 | element 1 .size. C |
| 0x0059f420 | g_MixerSampleRate_1 | undefined2 | element 1 .rate (0x2b11/0x5622). C |
| 0x0059f422 | g_MixerSampleFlags_1 | undefined2 | element 1 .flags (0x200 = in use). C |
| 0x0059fe10 | g_MixerVoiceSlots_1 | undefined4 | g_MixerVoiceSlots[1].pVoice (16 x 8 B at 0x59fe08). C |
| 0x0059fe14 | g_MixerVoiceSlotCmd_1 | undefined2 | g_MixerVoiceSlots[1] pending command word. C |
| 0x005a0334 | g_DSHwSamples_0_Inst1 | undefined4 | GAME sound lib: g_DSHwSamples[0] (0x5a0330, 128 x 0x5c) buffer instance [1]. C |
| 0x005a038c | g_DSHwSamples_1 | undefined4 | g_DSHwSamples[1] buffer instance [0]. C |
| 0x005a03cc | g_DSHwSamples_1_wfxFormatTagChannels | undefined4 | g_DSHwSamples[1] +0x40 WAVEFORMATEX wFormatTag/nChannels. C |
| 0x005a03d0 | g_DSHwSamples_1_wfxSamplesPerSec | undefined4 | ... nSamplesPerSec. C |
| 0x005a03d4 | g_DSHwSamples_1_wfxAvgBytesPerSec | undefined4 | ... nAvgBytesPerSec. C |
| 0x005a03d8 | g_DSHwSamples_1_wfxBlockAlignBits | undefined4 | ... nBlockAlign/wBitsPerSample. C |
| 0x005a03dc | g_DSHwSamples_1_wfxCbSize | undefined2 | ... cbSize. C |
| 0x005a03e0 | g_DSHwSampleInstCount_1 | undefined4 | g_DSHwSamples[1] +0x54 instance count. C |
| 0x005a03e4 | g_DSHwSampleBusyMask_1 | undefined4 | g_DSHwSamples[1] +0x58 busy-instance bitmask. C |
| 0x005a3144 | g_DSHwVoices_1_Flags | undefined4 | g_DSHwVoices (0x5a3138, 12 B each) [1].flags. C |
| 0x005a3148 | g_DSHwVoices_1_SampleIdx | undefined4 | g_DSHwVoices[1].sample index. C |
| 0x005a314c | g_DSHwVoices_1_InstIdx | undefined4 | g_DSHwVoices[1].instance index. C |
| 0x005a4e60 | _afxThreadData | pointer (applied) | MFC `_afxThreadData` (CThreadSlotData*, placement-constructed in buffer 0x5a4e68). C |
| 0x005a4ea0 | _afxCtl3dState | undefined4 | MFC `_afxCtl3dState` (CProcessLocal<_AFX_CTL3D_STATE>, m_pObject). C |
| 0x005a4ecc | afxData_hbrBtnFace | undefined4 | MFC `afxData` (AUX_DATA at 0x5a4ea8) +0x24 hbrBtnFace. C |
| 0x005a4edc | afxData_clrBtnText | undefined4 | afxData +0x34 clrBtnText. C |
| 0x005a4ee4 | afxData_hcurWait | undefined4 | afxData +0x3c hcurWait (DoWaitCursor). C |
| 0x005a4ee8 | afxData_hcurArrow | undefined4 | afxData +0x40 hcurArrow (AfxEndDeferRegisterClass). C |
| 0x005a4ef8 | afxData_hbmMenuDot | undefined4 | afxData +0x50 hbmMenuDot (_AfxLoadDotBitmap). C |
| 0x005a4efc | afxData_nWinVer | undefined4 | afxData +0x54 nWinVer (<0x333 test in ProcessMessageFilter). C |
| 0x005a4f04 | afxData_bWin4 | undefined4 | afxData +0x5c bWin4. C |
| 0x005a4f18 | _afxCriticalInit | undefined4 | MFC afxcrit.cpp `_afxCriticalInit`. C |
| 0x005a50b8 | _afxCriticalWin32s | undefined4 | MFC afxcrit.cpp `_afxCriticalWin32s` (no critical sections on Win32s). C |
| 0x005a51a0 | _pnhHeap | pointer (applied) | CRT new-handler (`_set_new_handler`), called by _callnewh. C |
| 0x005a52ac | pOldExceptFilter | pointer (applied) | CRT previous unhandled-exception filter saved by __CxxSetUnhandledExceptionFilter. C |
| 0x005a52b0 | tzapiused | undefined4 | CRT tzset.c: 1 = time zone came from GetTimeZoneInformation. C |
| 0x005a52b8 | tzinfo | undefined4 | CRT tzset.c `tzinfo` TIME_ZONE_INFORMATION (0x5a52b8..0x5a5363), field Bias. C |
| 0x005a52fc | tzinfo_StandardDate_wYear | undefined2 | tzinfo.StandardDate.wYear. C |
| 0x005a52fe | tzinfo_StandardDate_wMonth | undefined2 | tzinfo.StandardDate.wMonth. C |
| 0x005a5300 | tzinfo_StandardDate_wDayOfWeek | undefined2 | tzinfo.StandardDate.wDayOfWeek. C |
| 0x005a5302 | tzinfo_StandardDate_wDay | undefined2 | tzinfo.StandardDate.wDay. C |
| 0x005a5304 | tzinfo_StandardDate_wHour | undefined2 | tzinfo.StandardDate.wHour. C |
| 0x005a5306 | tzinfo_StandardDate_wMinute | undefined2 | tzinfo.StandardDate.wMinute. C |
| 0x005a5308 | tzinfo_StandardDate_wSecond | undefined2 | tzinfo.StandardDate.wSecond. C |
| 0x005a530a | tzinfo_StandardDate_wMilliseconds | undefined2 | tzinfo.StandardDate.wMilliseconds. C |
| 0x005a530c | tzinfo_StandardBias | undefined4 | tzinfo.StandardBias. C |
| 0x005a5350 | tzinfo_DaylightDate_wYear | undefined2 | tzinfo.DaylightDate.wYear. C |
| 0x005a5352 | tzinfo_DaylightDate_wMonth | undefined2 | tzinfo.DaylightDate.wMonth. C |
| 0x005a5354 | tzinfo_DaylightDate_wDayOfWeek | undefined2 | tzinfo.DaylightDate.wDayOfWeek. C |
| 0x005a5356 | tzinfo_DaylightDate_wDay | undefined2 | tzinfo.DaylightDate.wDay. C |
| 0x005a5358 | tzinfo_DaylightDate_wHour | undefined2 | tzinfo.DaylightDate.wHour. C |
| 0x005a535a | tzinfo_DaylightDate_wMinute | undefined2 | tzinfo.DaylightDate.wMinute. C |
| 0x005a535c | tzinfo_DaylightDate_wSecond | undefined2 | tzinfo.DaylightDate.wSecond. C |
| 0x005a535e | tzinfo_DaylightDate_wMilliseconds | undefined2 | tzinfo.DaylightDate.wMilliseconds. C |
| 0x005a5360 | tzinfo_DaylightBias | undefined4 | tzinfo.DaylightBias. C |
| 0x005a5368 | wcbuffer | undefined1 | CRT inithelp.c __getlocaleinfo static `wchar_t wcbuffer[4]`. C |
| 0x005a536a | wcbuffer_1 | undefined1 | phantom: wcbuffer[1]. C |
| 0x005a5370 | g_Ctl3dDeferSubclassHwnd | undefined4 | CTL3D (static ctl3d32s): window waiting for deferred subclass via WH_CALLWNDPROC hook (FUN_00449960). (Also hit by a phantom &wcbuffer[4] ref from __getlocaleinfo.) U |
| 0x005a5374 | g_Ctl3dDeferSubclassHook | undefined4 | CTL3D: HHOOK of that one-shot WH_CALLWNDPROC hook. U |
| 0x005a5378 | g_Ctl3dDeferSubclassProc | undefined4 | CTL3D: wndproc to install on deferred subclass. U |
| 0x005a53f0 | g_SndWaveFormat_cbSize | undefined2 | GAME sound lib: g_SndWaveFormat (WAVEFORMATEX at 0x5a53e0) .cbSize, zeroed in SndDrv_Init. C |
| 0x005a5580 | __piob | pointer (applied) | CRT `__piob` (FILE** stream table, malloc'd). C |
| 0x005a6590 | _nstream | undefined4 | CRT `_nstream` (number of stream slots). C |
| 0x005a6594 | __setlc_active | undefined4 | CRT `__setlc_active` (setlocale in progress; else `__unguarded_readlc_active` at 0x5a6598 is bumped). C |
| 0x005a65a0 | __pioinfo | undefined4 | CRT `__pioinfo[64]` lowio handle-info block pointers, [0]. C |
| 0x005a65a4 | __pioinfo_1 | undefined4 | `__pioinfo[1]` (__ioinit loop). C |
| 0x005a66a0 | _nhandle | undefined4 | CRT `_nhandle` (lowio handle count). C |
| 0x005a66a4 | _crtheap | HANDLE (applied) | CRT `_crtheap` (HeapCreate handle used by sbh/heap). C |
| 0x005a66a8 | __onexitend | pointer (applied) | CRT `__onexitend`. C |
| 0x005a66ac | __onexitbegin | pointer (applied) | CRT `__onexitbegin`. C |
| 0x005a66b0 | _acmdln | char * (applied) | CRT `_acmdln` (GetCommandLineA), used by __setargv/entry. C |
| 0x005a66e0 | g_Ctl3dEnabled | undefined4 | CTL3D `f3dDialogs`-style global enable flag (colour depth > 4 bpp and not 640x350). C |
| 0x005a66e4 | g_Ctl3dRegisterCount | undefined4 | CTL3D Ctl3dRegister/Unregister ref count (cInited). C |
| 0x005a66e8 | g_Ctl3dAtom_C3d | undefined2 | CTL3D window-prop atom "C3d". C |
| 0x005a66ea | g_Ctl3dAtom_C3dH | undefined2 | prop atom "C3dH". C |
| 0x005a66ec | g_Ctl3dAtom_C3dL | undefined2 | prop atom "C3dL". C |
| 0x005a66ee | g_Ctl3dAtom_C3dNew | undefined2 | prop atom "C3dNew" (stores original wndproc). C |
| 0x005a66f0 | g_Ctl3dAtom_C3dHNew | undefined2 | prop atom "C3dHNew". C |
| 0x005a66f2 | g_Ctl3dAtom_C3dLNew | undefined2 | prop atom "C3dLNew". C |
| 0x005a66f4 | g_Ctl3dAtom_C3dD | undefined2 | prop atom "C3dD" (subclass disabled marker). C |
| 0x005a66f8 | g_Ctl3dhinstLib | undefined4 | CTL3D resource instance (checkbox bitmap 0x67c7). C |
| 0x005a66fc | g_Ctl3dhmodLib | undefined4 | CTL3D module handle for SetWindowsHookEx. C |
| 0x005a6700 | g_Ctl3dVerWindows | undefined2 | CTL3D verWindows (major<<8\|minor, e.g. 0x35f = Win95). C |
| 0x005a6702 | g_Ctl3dSubclassMode | undefined2 | CTL3D subclass mode: 0x10 = deferred (hook) on old Win9x builds, 0x20 = immediate (NT / ver >= 0x35f). U |
| 0x005a6704 | g_Ctl3dClrt | undefined1 | CTL3D colour table clrt[8] (COLORREF), [0] rgbBtnHilite; order = COLOR_BTNHIGHLIGHT, BTNFACE, BTNSHADOW, BTNTEXT, WINDOW, WINDOWTEXT, GRAYTEXT, WINDOWFRAME. C |
| 0x005a6708 | g_Ctl3dClrt_rgbBtnFace | undefined4 | clrt[1] rgbBtnFace. C |
| 0x005a6710 | g_Ctl3dClrt_rgbBtnText | undefined4 | clrt[3] rgbBtnText. C |
| 0x005a671c | g_Ctl3dClrt_rgbGrayText | undefined4 | clrt[6] rgbGrayText. C |
| 0x005a6724 | g_Ctl3dBrt_hbrBtnHilite | undefined4 | CTL3D brush table brt[0] (hilite). C |
| 0x005a6728 | g_Ctl3dBrt_hbrBtnFace | undefined4 | brt[1] btn face (returned from WM_CTLCOLOR). C |
| 0x005a672c | g_Ctl3dBrt_hbrBtnShadow | undefined4 | brt[2] btn shadow. C |
| 0x005a6730 | g_Ctl3dhbmpCheckboxes | undefined4 | CTL3D recoloured checkbox/radio bitmap. C |
| 0x005a6734 | g_Ctl3dHookCacheThreadId | undefined4 | CTL3D last-looked-up hook thread id (cache). C |
| 0x005a6738 | g_Ctl3dHookCacheIndex | undefined4 | CTL3D cached hook-table index for that thread. C |
| 0x005a673c | g_Ctl3dHookCount | undefined4 | CTL3D number of hook records (max 0x80). C |
| 0x005a6740 | g_Ctl3dHooks | undefined4 | CTL3D hook table (20 B records) [0].hinstApp. C |
| 0x005a6744 | g_Ctl3dHooks_threadId | undefined4 | hook table field +4 thread id. C |
| 0x005a6748 | g_Ctl3dHooks_hhook | undefined4 | field +8 HHOOK (WH_CBT). C |
| 0x005a674c | g_Ctl3dHooks_cRef | undefined4 | field +0xc reference count. C |
| 0x005a6754 | g_Ctl3dHooks_1_hinstApp | undefined4 | phantom: hook record [1].hinstApp (compaction loop). C |
| 0x005a6758 | g_Ctl3dHooks_1_threadId | undefined4 | phantom: [1].threadId. C |
| 0x005a6760 | g_Ctl3dHooks_1_cRef | undefined4 | phantom: [1].cRef. C |
| 0x005a6768 | g_Ctl3dHooks_2_hinstApp | undefined4 | phantom: [2].hinstApp. C |
| 0x005a676c | g_Ctl3dHooks_2_threadId | undefined4 | phantom: [2].threadId. C |
| 0x005a6770 | g_Ctl3dHooks_2_hhook | undefined4 | phantom: [2].hhook. C |
| 0x005a6774 | g_Ctl3dHooks_2_cRef | undefined4 | phantom: [2].cRef. C |
| 0x005a7140 | g_Ctl3dSubclassTbl | undefined4 | CTL3D per-control-class table (6 x 0x18 B) [0].lpfnSubclass (ctl3d wndproc). C |
| 0x005a7144 | g_Ctl3dSubclassTbl_lpfnDefProc | undefined4 | table field +4 original class wndproc (GetClassInfo). C |
| 0x005a7158 | g_Ctl3dSubclassTbl_1 | undefined4 | phantom: table [1].lpfnSubclass. C |
| 0x005a71d0 | g_Ctl3dDefDlgProc | undefined4 | CTL3D original dialog-class (#32770) wndproc / DefDlgProcA. C |
| 0x005a71d8 | g_Ctl3dCyDlgFrameM1 | undefined4 | CTL3D GetSystemMetrics(SM_CYDLGFRAME)-1 (0x5a71d4 = SM_CXDLGFRAME-1). C |
| 0x005a71dc | g_Ctl3dCyCaption | undefined4 | CTL3D GetSystemMetrics(SM_CYCAPTION). C |
| 0x005a71e4 | g_Ctl3dChShortcut | undefined1 | CTL3D DBCS menu-mnemonic prefix char (0x1e/0x1f from kanjimenu/hangeulmenu). C |
| 0x005a71e5 | g_Ctl3dFDBCS | undefined1 | CTL3D fDBCS = GetSystemMetrics(SM_DBCSENABLED). C |

</details>
