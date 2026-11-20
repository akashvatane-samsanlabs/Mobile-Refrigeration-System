import {
  MODE_VALUE_LIMITS,
  OperatingModeCode,
  TEMP_LIMITS,
} from '../constants/device';
import { AppError } from './errors';

export type TemperaturePair = {
  minTenths: number;
  maxTenths: number;
};

export function celsiusToTenths(celsius: number): number {
  return Math.round(celsius * 10);
}

export function tenthsToCelsius(tenths: number): number {
  return tenths / 10;
}

export function formatTemperatureRangeLabel(): string {
  return `${TEMP_LIMITS.minCelsius}°C to ${TEMP_LIMITS.maxCelsius}°C`;
}

export function parseTemperatureInput(
  raw: string,
  field: 'tempMin' | 'tempMax' = 'tempMax',
): number {
  const label =
    field === 'tempMin' ? 'Minimum temperature' : 'Maximum temperature';
  const trimmed = raw.trim();

  if (!trimmed) {
    throw new AppError('INVALID_TEMPERATURE', `${label} is required.`, {
      field,
    });
  }

  const value = Number(trimmed);
  if (!Number.isFinite(value)) {
    throw new AppError(
      'INVALID_TEMPERATURE',
      `${label} must be a valid number.`,
      { field },
    );
  }

  if (value < TEMP_LIMITS.minCelsius || value > TEMP_LIMITS.maxCelsius) {
    throw new AppError(
      'INVALID_TEMPERATURE',
      `${label} must be between ${TEMP_LIMITS.minCelsius}°C and ${TEMP_LIMITS.maxCelsius}°C.`,
      { field },
    );
  }

  const tenths = celsiusToTenths(value);
  if (tenths < TEMP_LIMITS.minTenths || tenths > TEMP_LIMITS.maxTenths) {
    throw new AppError(
      'INVALID_TEMPERATURE',
      `${label} is outside the device range (${formatTemperatureRangeLabel()}).`,
      { field },
    );
  }

  return tenths;
}

export function parseTemperaturePair(
  minRaw: string,
  maxRaw: string,
): TemperaturePair {
  const minTenths = parseTemperatureInput(minRaw, 'tempMin');
  const maxTenths = parseTemperatureInput(maxRaw, 'tempMax');

  if (minTenths > maxTenths) {
    throw new AppError(
      'INVALID_TEMPERATURE_RANGE',
      `Minimum temperature (${tenthsToCelsius(minTenths).toFixed(
        1,
      )}°C) cannot be greater than maximum temperature (${tenthsToCelsius(
        maxTenths,
      ).toFixed(1)}°C).`,
      { field: 'tempMin' },
    );
  }

  return { minTenths, maxTenths };
}

export function getTemperatureFieldError(
  raw: string,
  field: 'tempMin' | 'tempMax',
): string | null {
  try {
    parseTemperatureInput(raw, field);
    return null;
  } catch (error) {
    if (error instanceof AppError) {
      return error.message;
    }
    return 'Invalid temperature value.';
  }
}

export function getTemperatureRangeError(
  minRaw: string,
  maxRaw: string,
): string | null {
  try {
    parseTemperaturePair(minRaw, maxRaw);
    return null;
  } catch (error) {
    if (error instanceof AppError) {
      return error.message;
    }
    return 'Invalid temperature range.';
  }
}

export function parseModeValueInput(raw: string): number {
  const trimmed = raw.trim();
  if (!trimmed) {
    throw new AppError('INVALID_MODE_VALUE', 'Mode value is required.', {
      field: 'modeValue',
    });
  }

  const value = Number(trimmed);
  if (!Number.isFinite(value) || !Number.isInteger(value)) {
    throw new AppError(
      'INVALID_MODE_VALUE',
      'Mode value must be a whole number.',
      { field: 'modeValue' },
    );
  }

  if (value < MODE_VALUE_LIMITS.min || value > MODE_VALUE_LIMITS.max) {
    throw new AppError(
      'INVALID_MODE_VALUE',
      `Mode value must be between ${MODE_VALUE_LIMITS.min} and ${MODE_VALUE_LIMITS.max}.`,
      { field: 'modeValue' },
    );
  }

  return value;
}

export function getModeValueFieldError(raw: string): string | null {
  try {
    parseModeValueInput(raw);
    return null;
  } catch (error) {
    if (error instanceof AppError) {
      return error.message;
    }
    return 'Invalid mode value.';
  }
}

/** Operating mode — firmware E / N / P / A command */
export function formatModeCommand(
  mode: OperatingModeCode,
  value: number,
): string {
  return `${mode}${value}`;
}

/** Max temperature — firmware T+ command */
export function formatMaxTemperatureCommand(tenths: number): string {
  return `T+${tenths}`;
}

/** Min temperature — firmware T- command */
export function formatMinTemperatureCommand(tenths: number): string {
  return `T-${tenths}`;
}

/** @deprecated Use formatMaxTemperatureCommand */
export function formatTemperatureCommand(tenths: number): string {
  return formatMaxTemperatureCommand(tenths);
}
