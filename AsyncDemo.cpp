/************************************************************
*                                                           *
* AsyncDemo.cpp                                             *
*                                                           *
* Copyright (c) Microsoft Corporation. All Rights Reserved. *
*                                                           *
************************************************************/

#define UNICODE
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include "resource.h"

//********************************************************************
//                                                  Global Variables  
//********************************************************************

// Context value structure.
typedef struct {
    HWND        hWindow;        // Handle for the dialog box
    HINTERNET   hConnect;       // Connection handle
    HINTERNET   hRequest;       // Resource request handle
    int         nURL;           // ID of the URL edit box
    int         nHeader;        // ID of the header output box
    int         nResource;      // ID of the resource output box
    DWORD       dwSize;         // Size of the latest data block
    DWORD       dwTotalSize;    // Size of the total data
    LPSTR       lpBuffer;       // Buffer for storing read data
    WCHAR       szMemo[256];    // String providing state information
} REQUEST_CONTEXT;


// Two Instances of the context value structure.
static REQUEST_CONTEXT rcContext, rcContext2;

// Session handle.
HINTERNET hSession;
WINHTTP_STATUS_CALLBACK pCallback = NULL;

CRITICAL_SECTION g_CallBackCritSec;

//********************************************************************
//                                                    Error Messages
//********************************************************************

// This macro returns the constant name in a string.
#define CASE_OF(constant)   case constant: return (L# constant)

LPCWSTR GetApiErrorString(DWORD dwResult)
{
    // Return the error result as a string so that the
    // name of the function causing the error can be displayed.
    switch(dwResult)
    {
        CASE_OF( API_RECEIVE_RESPONSE );
        CASE_OF( API_QUERY_DATA_AVAILABLE );
        CASE_OF( API_READ_DATA );
        CASE_OF( API_WRITE_DATA );
        CASE_OF( API_SEND_REQUEST );
    }
    return L"Unknown function";

}


//********************************************************************
//                                              Additional Functions  
//********************************************************************

void Cleanup (REQUEST_CONTEXT *cpContext)
{
    // Set the memo to indicate a closed handle.
    swprintf(cpContext->szMemo, L"Closed");

    if (cpContext->hRequest)
    {
        WinHttpSetStatusCallback(cpContext->hRequest, 
                NULL, 
                NULL, 
                NULL);

        WinHttpCloseHandle(cpContext->hRequest);
		cpContext->hRequest = NULL;
    }

    if (cpContext->hConnect)
    {
        WinHttpCloseHandle(cpContext->hConnect);
		cpContext->hConnect = NULL;
    }

    delete [] cpContext->lpBuffer;
	cpContext->lpBuffer = NULL;

    // note: this function can be called concurrently by differnet threads, therefore any global data
    // reference needs to be protected

    EnterCriticalSection(&g_CallBackCritSec);

    // If both handles are closed, re-enable the download button.
    if ((wcsncmp( rcContext.szMemo, L"Closed",6)==0) &&
        (wcsncmp( rcContext2.szMemo, L"Closed",6)==0))
    {
        EnableWindow( GetDlgItem(cpContext->hWindow, IDC_DOWNLOAD),1);
    }

    LeaveCriticalSection(&g_CallBackCritSec);

}

// Forward declaration.
void __stdcall AsyncCallback(HINTERNET, DWORD_PTR, DWORD, LPVOID, DWORD);

BOOL SendRequest(REQUEST_CONTEXT *cpContext, LPWSTR szURL)
{
    WCHAR szHost[256];
    DWORD dwOpenRequestFlag = 0;
    URL_COMPONENTS urlComp;
    BOOL fRet = FALSE;

    // Initialize URL_COMPONENTS structure.
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);

    // Use allocated buffer to store the Host Name.
    urlComp.lpszHostName        = szHost;
    urlComp.dwHostNameLength    = sizeof(szHost) / sizeof(szHost[0]);

    // Set non zero lengths to obtain pointer to the URL Path.
    /* note: if we threat this pointer as a NULL terminated string
            this pointer will contain Extra Info as well. */
    urlComp.dwUrlPathLength = -1;

    // Crack HTTP scheme.
    urlComp.dwSchemeLength = -1;

    // Set the szMemo string.
    swprintf( cpContext->szMemo, L"WinHttpCrackURL (%d)", cpContext->nURL);

    // Crack the URL.
    if (!WinHttpCrackUrl(szURL, 0, 0, &urlComp))
    {
        goto cleanup;
    }

    // Set the szMemo string.
    swprintf( cpContext->szMemo, L"WinHttpConnect (%d)", cpContext->nURL);

	// Set the szMemo string.
	swprintf(cpContext->szMemo, L"WinHttpSetStatusCallback (%d)", cpContext->nURL);

	// Install the status callback function.
	if (pCallback == NULL)
	{
		pCallback = WinHttpSetStatusCallback(hSession,
			(WINHTTP_STATUS_CALLBACK)AsyncCallback,
			WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
			NULL);
	}
	// note: On success WinHttpSetStatusCallback returns the previously defined callback function.
	// Here it should be NULL
	if (pCallback == WINHTTP_INVALID_STATUS_CALLBACK)
	{
		goto cleanup;
	}

    // Open an HTTP session.
    cpContext->hConnect = WinHttpConnect(hSession, szHost, 
                                    urlComp.nPort, 0);
    if (NULL == cpContext->hConnect)
    {
        goto cleanup;
    }
	WINHTTP_AUTOPROXY_OPTIONS AutoProxyOptions = { 0 };
	WINHTTP_CURRENT_USER_IE_PROXY_CONFIG IEProxyConfig;
	WINHTTP_PROXY_INFO  proxyInfo = { 0 };
    if (WinHttpGetIEProxyConfigForCurrentUser(&IEProxyConfig))
    {
        //
        // If IE is configured to autodetect, then we'll autodetect too
        //
        if (IEProxyConfig.fAutoDetect)
        {
            AutoProxyOptions.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;

            //
            // Use both DHCP and DNS-based autodetection
            //
            AutoProxyOptions.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | 
                                                 WINHTTP_AUTO_DETECT_TYPE_DNS_A;

        }

        //
        // If there's an autoconfig URL stored in the IE proxy settings, save it
        //
        if (IEProxyConfig.lpszAutoConfigUrl)
        {
            AutoProxyOptions.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
            AutoProxyOptions.lpszAutoConfigUrl = IEProxyConfig.lpszAutoConfigUrl;
            
        }
		BOOL bResult=WinHttpGetProxyForUrl(hSession, 
										urlComp.lpszScheme, 
                                         &AutoProxyOptions, 
                                         &proxyInfo);
		DWORD dwError;
		if (!bResult)
		{
			dwError=GetLastError();
		}
		else
		{
			 if (!WinHttpSetOption(hSession, 
                          WINHTTP_OPTION_PROXY,  
                          &proxyInfo, 
                          sizeof(proxyInfo)))
			{
				dwError=GetLastError();
			}
		}

	}
    // Prepare OpenRequest flag
    dwOpenRequestFlag = (INTERNET_SCHEME_HTTPS == urlComp.nScheme) ?
                            WINHTTP_FLAG_SECURE : 0;

    // Set the szMemo string.
    swprintf( cpContext->szMemo, L"WinHttpOpenRequest (%d)", cpContext->nURL);

    // Open a "GET" request.
    cpContext->hRequest = WinHttpOpenRequest(cpContext->hConnect, 
                                        L"GET", urlComp.lpszUrlPath,
                                        NULL, WINHTTP_NO_REFERER, 
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        dwOpenRequestFlag);

    if (cpContext->hRequest == 0)
    {
        goto cleanup;
    }


    // Set the szMemo string.
    swprintf( cpContext->szMemo, L"WinHttpSendRequest (%d)", cpContext->nURL);

    // Send the request.
    if (!WinHttpSendRequest(cpContext->hRequest, 
                        WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                        WINHTTP_NO_REQUEST_DATA, 0, 0, 
                        (DWORD_PTR)cpContext))
    {
        goto cleanup;
    }

    fRet = TRUE;
 
cleanup:

    if (fRet == FALSE)
    {
        WCHAR szError[256];

        // Set the error message.
        swprintf(szError, L"%s failed with error %d", cpContext->szMemo, GetLastError());

        // Cleanup handles.
        Cleanup(cpContext);
                    
        // Display the error message.
        SetDlgItemText(cpContext->hWindow, cpContext->nResource, szError);

    }
                
    return fRet;
}

BOOL Header(REQUEST_CONTEXT *cpContext) 
{
    DWORD dwSize=0;
    LPVOID lpOutBuffer = NULL;

    // Set the state memo.
    swprintf(cpContext->szMemo, L"WinHttpQueryHeaders (%d)", cpContext->nURL);

    // Use HttpQueryInfo to obtain the size of the buffer.
    if (!WinHttpQueryHeaders( cpContext->hRequest, 
                              WINHTTP_QUERY_RAW_HEADERS_CRLF,
                              WINHTTP_HEADER_NAME_BY_INDEX, NULL, &dwSize, WINHTTP_NO_HEADER_INDEX))
    {
        // An ERROR_INSUFFICIENT_BUFFER is expected because you
        // are looking for the size of the headers.  If any other
        // error is encountered, display error information.
        DWORD dwErr = GetLastError();
        if (dwErr != ERROR_INSUFFICIENT_BUFFER)
        {
            WCHAR  szError[256];
            swprintf( szError, L"%s: Error %d encountered.",
                      cpContext->szMemo, dwErr);
            SetDlgItemText( cpContext->hWindow, cpContext->nResource, szError);
            return FALSE;
        }
    }
    
    // Allocate memory for the buffer.
    lpOutBuffer = new WCHAR[dwSize];

    // Use HttpQueryInfo to obtain the header buffer.
    if(WinHttpQueryHeaders( cpContext->hRequest, 
                            WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, lpOutBuffer, &dwSize, WINHTTP_NO_HEADER_INDEX))
        SetDlgItemText( cpContext->hWindow, cpContext->nHeader, 
                        (LPWSTR)lpOutBuffer);

    // Free the allocated memory.
    delete [] lpOutBuffer;

    return TRUE;
}


BOOL QueryData(REQUEST_CONTEXT *cpContext)
{
    // Set the state memo.
    swprintf( cpContext->szMemo, L"WinHttpQueryDataAvailable (%d)", 
              cpContext->nURL);
        
    // Chech for available data.
    if (WinHttpQueryDataAvailable(cpContext->hRequest, NULL) == FALSE)
    {
        // If a synchronous error occured, display the error.  Otherwise
        // the query is successful or asynchronous.
        DWORD dwErr = GetLastError();
        WCHAR  szError[256];
        swprintf( szError, L"%s: Error %d encountered.",
                  cpContext->szMemo, dwErr);
        SetDlgItemText( cpContext->hWindow, cpContext->nResource, szError);
        return FALSE;
    }
    return TRUE;
}


void TransferAndDeleteBuffers(REQUEST_CONTEXT *cpContext, LPSTR lpReadBuffer, DWORD dwBytesRead)
{
    cpContext->dwSize = dwBytesRead;

    if(!cpContext->lpBuffer)
    {
        // If there is no context buffer, start one with the read data.
        cpContext->lpBuffer = lpReadBuffer;
    }
    else
    {
        // Store the previous buffer, and create a new one big
        // enough to hold the old data and the new data.
        LPSTR lpOldBuffer = cpContext->lpBuffer;
        cpContext->lpBuffer = new char[cpContext->dwTotalSize + cpContext->dwSize];

        // Copy the old and read buffer into the new context buffer.
        memcpy(cpContext->lpBuffer, lpOldBuffer, cpContext->dwTotalSize);
        memcpy(cpContext->lpBuffer + cpContext->dwTotalSize, lpReadBuffer, cpContext->dwSize);

        // Free the memory allocated to the old and read buffers.
        delete [] lpOldBuffer;
        delete [] lpReadBuffer;
    }
    
    // Keep track of the total size.
    cpContext->dwTotalSize += cpContext->dwSize;
}


BOOL ReadData(REQUEST_CONTEXT *cpContext)
{
    LPSTR lpOutBuffer = new char[cpContext->dwSize+1];
    ZeroMemory(lpOutBuffer, cpContext->dwSize+1);

    // Set the state memo.
    swprintf( cpContext->szMemo, L"WinHttpReadData (%d)", 
              cpContext->nURL);

    // Read the available data.
    if (WinHttpReadData( cpContext->hRequest, (LPVOID)lpOutBuffer, 
                          cpContext->dwSize, NULL) == FALSE)
    {
        // If a synchronous error occurred, display the error.  Otherwise
        // the read is successful or asynchronous.
        DWORD dwErr = GetLastError();
        WCHAR  szError[256];
        swprintf( szError, L"%s: Error %d encountered.",
                  cpContext->szMemo, dwErr);
        SetDlgItemText( cpContext->hWindow, cpContext->nResource, szError);
        delete [] lpOutBuffer;
        return FALSE;
    }

    return TRUE;
}


//********************************************************************
//                                                   Status Callback  
//********************************************************************

void __stdcall AsyncCallback( HINTERNET hInternet, DWORD_PTR dwContext,
                              DWORD dwInternetStatus,
                              LPVOID lpvStatusInformation,
                              DWORD dwStatusInformationLength)
{
    REQUEST_CONTEXT *cpContext;
    WCHAR szBuffer[256];
    cpContext = (REQUEST_CONTEXT*)dwContext;
    WINHTTP_ASYNC_RESULT *pAR;

    if (cpContext == NULL)
    {
        // this should not happen, but we are being defensive here
        return;
    }

    szBuffer[0] = 0;

    // Create a string that reflects the status flag.
    switch (dwInternetStatus)
    {
	case WINHTTP_CALLBACK_STATUS_CLOSING_CONNECTION:
		//Closing the connection to the server.The lpvStatusInformation parameter is NULL.
		swprintf(szBuffer, L"%s: CLOSING_CONNECTION (%d)", cpContext->szMemo, dwStatusInformationLength);
		break;

	case WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER:
		//Successfully connected to the server. 
		//The lpvStatusInformation parameter contains a pointer to an LPWSTR that indicates the IP address of the server in dotted notation.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: CONNECTED_TO_SERVER (%s)", cpContext->szMemo, (char *)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: CONNECTED_TO_SERVER (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_CONNECTING_TO_SERVER:
		//Connecting to the server.
		//The lpvStatusInformation parameter contains a pointer to an LPWSTR that indicates the IP address of the server in dotted notation.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: CONNECTING_TO_SERVER (%s)", cpContext->szMemo, (char *)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: CONNECTING_TO_SERVER (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_CONNECTION_CLOSED:
		//Successfully closed the connection to the server. The lpvStatusInformation parameter is NULL. 
		swprintf(szBuffer, L"%s: CONNECTION_CLOSED (%d)", cpContext->szMemo, dwStatusInformationLength);
		break;

	case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
		//Data is available to be retrieved with WinHttpReadData.The lpvStatusInformation parameter points to a DWORD that contains the number of bytes of data available.
		//The dwStatusInformationLength parameter itself is 4 (the size of a DWORD).
		swprintf(szBuffer, L"%s: DATA_AVAILABLE (%d)", cpContext->szMemo, dwStatusInformationLength);

		cpContext->dwSize = *((LPDWORD)lpvStatusInformation);

		// If there is no data, the process is complete.
		if (cpContext->dwSize == 0)
		{
			// All of the data has been read.  Display the data.
			if (cpContext->dwTotalSize)
			{
				// Convert the final context buffer to wide characters,
				// and display.
				LPWSTR lpWideBuffer = new WCHAR[cpContext->dwTotalSize + 1];
				MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED,
					cpContext->lpBuffer,
					cpContext->dwTotalSize,
					lpWideBuffer,
					cpContext->dwTotalSize);
				lpWideBuffer[cpContext->dwTotalSize] = 0;
				/* note: in the case of binary data, only data upto the first null will be displayed */
				SetDlgItemText(cpContext->hWindow, cpContext->nResource, lpWideBuffer);

				// Delete the remaining data buffers.
				delete[] lpWideBuffer;
				delete[] cpContext->lpBuffer;
				cpContext->lpBuffer = NULL;
			}

			// Close the request and connect handles for this context.
			Cleanup(cpContext);

		}
		else
			// Otherwise, read the next block of data.
			if (ReadData(cpContext) == FALSE)
			{
				Cleanup(cpContext);
			}
		break;

	case WINHTTP_CALLBACK_STATUS_HANDLE_CREATED:
		//An HINTERNET handle has been created. The lpvStatusInformation parameter contains a pointer to the HINTERNET handle.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: HANDLE_CREATED (%X)", cpContext->szMemo, (char *)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: HANDLE_CREATED (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
		//This handle value has been terminated. The lpvStatusInformation parameter contains a pointer to the HINTERNET handle. There will be no more callbacks for this handle.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: HANDLE_CLOSING (%X)", cpContext->szMemo, (char *)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: HANDLE_CLOSING (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
		//The response header has been received and is available with WinHttpQueryHeaders. The lpvStatusInformation parameter is NULL.
		swprintf(szBuffer, L"%s: HEADERS_AVAILABLE (%d)", cpContext->szMemo, dwStatusInformationLength);
		Header(cpContext);

		// Initialize the buffer sizes.
		cpContext->dwSize = 0;
		cpContext->dwTotalSize = 0;

		// Begin downloading the resource.
		if (QueryData(cpContext) == FALSE)
		{
			Cleanup(cpContext);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_INTERMEDIATE_RESPONSE:
		//Received an intermediate (100 level) status code message from the server. 
		//The lpvStatusInformation parameter contains a pointer to a DWORD that indicates the status code.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: INTERMEDIATE_RESPONSE (%d)", cpContext->szMemo, *(DWORD*)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: INTERMEDIATE_RESPONSE (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_NAME_RESOLVED:
		//Successfully found the IP address of the server. The lpvStatusInformation parameter contains a pointer to a LPWSTR that indicates the name that was resolved.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: NAME_RESOLVED (%s)", cpContext->szMemo, (char *)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: NAME_RESOLVED (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;


	case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
		//Data was successfully read from the server. The lpvStatusInformation parameter contains a pointer to the buffer specified in the call to WinHttpReadData. 
		//The dwStatusInformationLength parameter contains the number of bytes read.
		//When used by WinHttpWebSocketReceive, the lpvStatusInformation parameter contains a pointer to a WINHTTP_WEB_SOCKET_STATUS structure, 
		//	and the dwStatusInformationLength parameter indicates the size of lpvStatusInformation.

		swprintf(szBuffer, L"%s: READ_COMPLETE (%d)", cpContext->szMemo, dwStatusInformationLength);

		// Copy the data and delete the buffers.

		if (dwStatusInformationLength != 0)
		{
			TransferAndDeleteBuffers(cpContext, (LPSTR)lpvStatusInformation, dwStatusInformationLength);

			// Check for more data.
			if (QueryData(cpContext) == FALSE)
			{
				Cleanup(cpContext);
			}
		}
		break;


	case WINHTTP_CALLBACK_STATUS_RECEIVING_RESPONSE:
		//Waiting for the server to respond to a request. The lpvStatusInformation parameter is NULL. 
		swprintf(szBuffer, L"%s: RECEIVING_RESPONSE (%d)", cpContext->szMemo, dwStatusInformationLength);
		break;

	case WINHTTP_CALLBACK_STATUS_REDIRECT:
		//An HTTP request is about to automatically redirect the request. The lpvStatusInformation parameter contains a pointer to an LPWSTR indicating the new URL.
		//At this point, the application can read any data returned by the server with the redirect response and can query the response headers. It can also cancel the operation by closing the handle

		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: REDIRECT (%s)", cpContext->szMemo, (char *)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: REDIRECT (%d)", cpContext->szMemo, dwStatusInformationLength);
		}		
		break;

	case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
		//An error occurred while sending an HTTP request. 
		//The lpvStatusInformation parameter contains a pointer to a WINHTTP_ASYNC_RESULT structure. Its dwResult member indicates the ID of the called function and dwError indicates the return value.
		pAR = (WINHTTP_ASYNC_RESULT *)lpvStatusInformation;
		swprintf(szBuffer, L"%s: REQUEST_ERROR - error %d, result %s", cpContext->szMemo, pAR->dwError, GetApiErrorString(pAR->dwResult));

		Cleanup(cpContext);
		break;

	case WINHTTP_CALLBACK_STATUS_REQUEST_SENT:
		//Successfully sent the information request to the server. 
		//The lpvStatusInformation parameter contains a pointer to a DWORD indicating the number of bytes sent. 
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: REQUEST_SENT (%d)", cpContext->szMemo, *(DWORD*)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: REQUEST_SENT (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_RESOLVING_NAME:
		//Looking up the IP address of a server name. The lpvStatusInformation parameter contains a pointer to the server name being resolved.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: RESOLVING_NAME (%s)", cpContext->szMemo, (char *)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: RESOLVING_NAME (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_RESPONSE_RECEIVED:
		//Successfully received a response from the server. 
		//The lpvStatusInformation parameter contains a pointer to a DWORD indicating the number of bytes received.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: RESPONSE_RECEIVED (%d)", cpContext->szMemo, *(DWORD*)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: RESPONSE_RECEIVED (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
		//One or more errors were encountered while retrieving a Secure Sockets Layer (SSL) certificate from the server. 
		//The lpvStatusInformation parameter contains a flag. For more information, see the description for lpvStatusInformation.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: SECURE_FAILURE (%d)", cpContext->szMemo, *(DWORD*)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: SECURE_FAILURE (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_SENDING_REQUEST:
		// Sending the information request to the server.The lpvStatusInformation parameter is NULL.
		swprintf(szBuffer, L"%s: SENDING_REQUEST (%d)", cpContext->szMemo, dwStatusInformationLength);
		break;

	case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            swprintf(szBuffer,L"%s: SENDREQUEST_COMPLETE (%d)",  cpContext->szMemo, dwStatusInformationLength);

            // Prepare the request handle to receive a response.
            if (WinHttpReceiveResponse( cpContext->hRequest, NULL) == FALSE)
            {
                Cleanup(cpContext);
            }
            break;

	case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
		//Data was successfully written to the server. The lpvStatusInformation parameter contains a pointer to a DWORD that indicates the number of bytes written.
		//When used by WinHttpWebSocketSend, the lpvStatusInformation parameter contains a pointer to a WINHTTP_WEB_SOCKET_STATUS structure, 
		//and the dwStatusInformationLength parameter indicates the size of lpvStatusInformation.
		if (lpvStatusInformation)
		{
			swprintf(szBuffer, L"%s: WRITE_COMPLETE (%d)", cpContext->szMemo, *(DWORD*)lpvStatusInformation);
		}
		else
		{
			swprintf(szBuffer, L"%s: WRITE_COMPLETE (%d)", cpContext->szMemo, dwStatusInformationLength);
		}
		break;

	case WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE:
		// The operation initiated by a call to WinHttpGetProxyForUrlEx is complete. Data is available to be retrieved with WinHttpReadData.
		swprintf(szBuffer, L"%s: GETPROXYFORURL_COMPLETE (%d)", cpContext->szMemo, dwStatusInformationLength);
		break;

	case WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE:
		// The connection was successfully closed via a call to WinHttpWebSocketClose.
		swprintf(szBuffer, L"%s: CLOSE_COMPLETE (%d)", cpContext->szMemo, dwStatusInformationLength);
		break;

	case WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE:
		// The connection was successfully shut down via a call to WinHttpWebSocketShutdown
		swprintf(szBuffer, L"%s: SHUTDOWN_COMPLETE (%d)", cpContext->szMemo, dwStatusInformationLength);
		break;

	default:
            swprintf(szBuffer,L"%s: Unknown/unhandled callback - status %d given", cpContext->szMemo, dwInternetStatus);
            break;
    }

    // Add the callback information to the listbox.
	LRESULT index = SendDlgItemMessage( cpContext->hWindow, IDC_CBLIST, LB_ADDSTRING, 0, (LPARAM)szBuffer);

	SendDlgItemMessage(cpContext->hWindow, IDC_CBLIST, LB_SETTOPINDEX, index, 0);

}


//********************************************************************
//                                                   Dialog Function  
//********************************************************************

BOOL CALLBACK AsyncDialog( HWND hX, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
    case WM_INITDIALOG:
        // Set the default web sites.
        SetDlgItemText(hX, IDC_URL1, L"http://www.microsoft.com");

        // Initialize the first context value.
        rcContext.hWindow = hX;
        rcContext.nURL = IDC_URL1;
        rcContext.nHeader = IDC_HEADER1;
        rcContext.nResource = IDC_RESOURCE1;
        rcContext.hConnect = 0;
        rcContext.hRequest = 0;
		rcContext.lpBuffer = NULL;
        rcContext.szMemo[0] = 0;


        return TRUE;
	case WM_CLOSE:
        EndDialog(hX,0);
		return TRUE;
    case WM_COMMAND:
        switch(LOWORD(wParam))
        {
            case IDC_EXIT:
                EndDialog(hX,0);
                return TRUE;
            case IDC_DOWNLOAD:
                WCHAR szURL[256];

                // Disable the download button.
                EnableWindow( GetDlgItem(hX, IDC_DOWNLOAD), 0);

                // Reset the edit boxes.
                SetDlgItemText( hX, IDC_HEADER1, NULL );
                SetDlgItemText( hX, IDC_RESOURCE1, NULL );
				SendDlgItemMessage(hX, IDC_CBLIST, LB_RESETCONTENT, 0, NULL);

                // Obtain the URLs from the dialog box and send the request.
                GetDlgItemText( hX, IDC_URL1, szURL, 256);
                BOOL fRequest = SendRequest(&rcContext, szURL);
 
                // Enable the download button if both request are failing.
                EnableWindow( GetDlgItem(hX, IDC_DOWNLOAD), TRUE);

                return (fRequest);
        }
    default:
        return FALSE;
    }
}


//********************************************************************
//                                                      Main Program  
//********************************************************************

int WINAPI WinMain( HINSTANCE hThisInst, HINSTANCE hPrevInst,
                    LPSTR lpszArgs, int nWinMode)
{
    // Create the session handle using the default settings.
    hSession = WinHttpOpen( L"Asynchronous WinHTTP Demo/1.0", 
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS,
                            WINHTTP_FLAG_ASYNC);

    // Check to see if the session handle was successfully created.
    if(hSession != NULL)
    {   
        InitializeCriticalSection(&g_CallBackCritSec);

        // Show the dialog box.
        DialogBox( hThisInst, MAKEINTRESOURCE(IDD_DIALOG1), 
                   HWND_DESKTOP, (DLGPROC)AsyncDialog );

        DeleteCriticalSection(&g_CallBackCritSec);

        // Close the session handle.
        WinHttpCloseHandle(hSession);

    }

    return 0;
}