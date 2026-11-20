export type AppErrorCode =
  | 'BLUETOOTH_OFF'
  | 'PERMISSION_DENIED'
  | 'DISCOVERY_FAILED'
  | 'DEVICE_NOT_FOUND'
  | 'PAIRING_FAILED'
  | 'CONNECTION_FAILED'
  | 'CONNECTION_LOST'
  | 'NOT_CONNECTED'
  | 'WRITE_FAILED'
  | 'INVALID_TEMPERATURE'
  | 'INVALID_TEMPERATURE_RANGE'
  | 'INVALID_MODE_VALUE'
  | 'TIMEOUT'
  | 'UNKNOWN';

export type AppErrorField = 'tempMin' | 'tempMax' | 'modeValue';

export class AppError extends Error {
  readonly code: AppErrorCode;
  readonly field?: AppErrorField;
  readonly cause?: unknown;

  constructor(
    code: AppErrorCode,
    message: string,
    options?: { cause?: unknown; field?: AppErrorField },
  ) {
    super(message);
    this.name = 'AppError';
    this.code = code;
    this.cause = options?.cause;
    this.field = options?.field;
  }
}

export function isAppError(error: unknown): error is AppError {
  return error instanceof AppError;
}

function rawMessage(error: unknown): string {
  if (error instanceof Error) {
    return error.message ?? '';
  }
  if (typeof error === 'string') {
    return error;
  }
  return '';
}

/** Native Bluetooth errors are technical; map the common ones to plain text */
function friendlyNativeMessage(message: string): AppError | null {
  const text = message.toLowerCase();

  if (text.includes('not enabled') || text.includes('bluetooth is off')) {
    return new AppError(
      'BLUETOOTH_OFF',
      'Bluetooth is turned off. Turn it on and try again.',
    );
  }
  if (text.includes('permission') || text.includes('securityexception')) {
    return new AppError(
      'PERMISSION_DENIED',
      'Bluetooth permission is missing. Open app Settings and allow Nearby devices.',
    );
  }
  if (
    text.includes('read failed') ||
    text.includes('socket') ||
    text.includes('connection refused') ||
    text.includes('host is down')
  ) {
    return new AppError(
      'CONNECTION_FAILED',
      'The device did not accept the connection. Make sure it is powered on, nearby and not connected to another phone.',
    );
  }
  if (text.includes('timeout') || text.includes('timed out')) {
    return new AppError(
      'TIMEOUT',
      'The device took too long to respond. Move closer and try again.',
    );
  }
  return null;
}

export function toUserMessage(error: unknown): string {
  if (error instanceof AppError) {
    return error.message;
  }

  const message = rawMessage(error);
  return (
    friendlyNativeMessage(message)?.message ??
    (message || 'An unexpected error occurred.')
  );
}

/** Technical message of the underlying error, for the device log */
export function getErrorDetail(error: unknown): string | null {
  const source = error instanceof AppError ? error.cause : error;
  const message = rawMessage(source);
  return message && message !== rawMessage(error) ? message : null;
}

export function getErrorTitle(code: AppErrorCode): string {
  switch (code) {
    case 'BLUETOOTH_OFF':
      return 'Bluetooth off';
    case 'PERMISSION_DENIED':
      return 'Permission denied';
    case 'DISCOVERY_FAILED':
    case 'DEVICE_NOT_FOUND':
      return 'Device not found';
    case 'PAIRING_FAILED':
      return 'Pairing failed';
    case 'CONNECTION_FAILED':
    case 'NOT_CONNECTED':
      return 'Connection error';
    case 'CONNECTION_LOST':
      return 'Connection lost';
    case 'WRITE_FAILED':
      return 'Send failed';
    case 'INVALID_TEMPERATURE':
    case 'INVALID_TEMPERATURE_RANGE':
      return 'Invalid temperature';
    case 'INVALID_MODE_VALUE':
      return 'Invalid mode value';
    case 'TIMEOUT':
      return 'Timed out';
    default:
      return 'Error';
  }
}

export function wrapUnknown(error: unknown, fallback: string): AppError {
  if (error instanceof AppError) {
    return error;
  }

  const friendly = friendlyNativeMessage(rawMessage(error));
  if (friendly) {
    return new AppError(friendly.code, friendly.message, { cause: error });
  }

  return new AppError('UNKNOWN', fallback, { cause: error });
}

export function withTimeout<T>(
  promise: Promise<T>,
  ms: number,
  message: string,
): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  const timeout = new Promise<never>((_, reject) => {
    timer = setTimeout(() => reject(new AppError('TIMEOUT', message)), ms);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}
