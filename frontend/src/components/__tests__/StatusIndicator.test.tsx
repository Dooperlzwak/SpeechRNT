import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import StatusIndicator from '../StatusIndicator';

describe('StatusIndicator', () => {
  it('should display idle state correctly', () => {
    render(<StatusIndicator state="idle" />);
    
    expect(screen.getByText('Waiting')).toBeInTheDocument();
  });

  it('should display listening state with animation', () => {
    render(<StatusIndicator state="listening" />);
    
    const badge = screen.getByText('Listening').closest('.animate-pulse');
    expect(screen.getByText('Listening')).toBeInTheDocument();
    expect(badge).toHaveClass('animate-pulse');
    expect(badge).toHaveClass('bg-blue-500');
  });

  it('should display thinking state', () => {
    render(<StatusIndicator state="thinking" />);
    
    const badge = screen.getByText('Thinking').closest('.bg-yellow-500');
    expect(screen.getByText('Thinking')).toBeInTheDocument();
    expect(badge).toHaveClass('bg-yellow-500');
  });

  it('should display speaking state with animation', () => {
    render(<StatusIndicator state="speaking" />);
    
    const badge = screen.getByText('Speaking').closest('.animate-pulse');
    expect(screen.getByText('Speaking')).toBeInTheDocument();
    expect(badge).toHaveClass('animate-pulse');
    expect(badge).toHaveClass('bg-green-500');
  });

  it('should apply custom className', () => {
    render(<StatusIndicator state="idle" className="custom-class" />);
    
    const badge = screen.getByText('Waiting').closest('.custom-class');
    expect(badge).toHaveClass('custom-class');
  });
});