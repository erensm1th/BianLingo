"""Embed verified package files as PE resources before optional code signing."""
import ctypes
from ctypes import wintypes
import hashlib
import io
import json
import os
from zipfile import ZipFile, ZIP_DEFLATED


def make_payload(root, names):
    files = {}
    output = io.BytesIO()
    with ZipFile(output, 'w', compression=ZIP_DEFLATED, compresslevel=6) as archive:
        for name in sorted(names):
            data = (root / name).read_bytes()
            archive.writestr(name, data)
            files[name] = dict(size=len(data), sha256=hashlib.sha256(data).hexdigest())
    payload = output.getvalue()
    metadata = dict(schema=1, sha256=hashlib.sha256(payload).hexdigest(), files=files)
    return payload, json.dumps(metadata, sort_keys=True, separators=(',', ':')).encode('utf8')


def embed_resources(executable, payload, metadata):
    if os.name != 'nt':
        raise ValueError('Windows package embedding requires Windows')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    kernel.BeginUpdateResourceW.restype = wintypes.HANDLE
    kernel.UpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                      wintypes.WORD, wintypes.LPVOID, wintypes.DWORD]
    kernel.UpdateResourceW.restype = wintypes.BOOL
    kernel.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    kernel.EndUpdateResourceW.restype = wintypes.BOOL
    handle = kernel.BeginUpdateResourceW(str(executable.resolve()), False)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        kind = ctypes.cast(ctypes.c_void_p(10), wintypes.LPCWSTR)  # RT_RCDATA
        for identifier, data in [(301, payload), (302, metadata)]:
            name = ctypes.cast(ctypes.c_void_p(identifier), wintypes.LPCWSTR)
            buffer = ctypes.create_string_buffer(data)
            if not kernel.UpdateResourceW(handle, kind, name, 0, buffer, len(data)):
                raise ctypes.WinError(ctypes.get_last_error())
        if not kernel.EndUpdateResourceW(handle, False):
            raise ctypes.WinError(ctypes.get_last_error())
        handle = None
    finally:
        if handle:
            kernel.EndUpdateResourceW(handle, True)
    actual = read_resources(executable)
    if actual != (payload, metadata):
        raise ValueError('Embedded package resources differ from verified input')


def read_resources(executable):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    kernel.LoadLibraryExW.restype = wintypes.HMODULE
    kernel.FindResourceW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR, wintypes.LPCWSTR]
    kernel.FindResourceW.restype = wintypes.HANDLE
    kernel.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel.SizeofResource.restype = wintypes.DWORD
    kernel.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel.LoadResource.restype = wintypes.HANDLE
    kernel.LockResource.argtypes = [wintypes.HANDLE]
    kernel.LockResource.restype = wintypes.LPVOID
    kernel.FreeLibrary.argtypes = [wintypes.HMODULE]
    module = kernel.LoadLibraryExW(str(executable.resolve()), None, 0x02 | 0x20)
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        output = []
        for identifier in [301, 302]:
            resource = kernel.FindResourceW(module, ctypes.cast(ctypes.c_void_p(identifier), wintypes.LPCWSTR),
                                            ctypes.cast(ctypes.c_void_p(10), wintypes.LPCWSTR))
            if not resource:
                raise ValueError('Missing embedded package resource')
            size = kernel.SizeofResource(module, resource)
            data = kernel.LockResource(kernel.LoadResource(module, resource))
            if not size or not data:
                raise ValueError('Empty embedded package resource')
            output.append(ctypes.string_at(data, size))
        return tuple(output)
    finally:
        kernel.FreeLibrary(module)
